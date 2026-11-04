#include <cysynck/core.hpp>
#include <httplib.h>
#include <functional>
#include <iostream>
#include <thread>
using namespace cysynck;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &call, const char *message) {
    try {
        call();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
static Json act(Core &core, const std::string &name, const Json &data = Json::object(),
                const std::string &project = "team") {
    return core.request("POST", "/api/projects/" + project + "/" + name, data);
}
static Json load(Core &core) { return Json::parse(read_file(core.state() / "registry.json")); }
static void save(Core &core, const Json &data) {
    atomic_write(core.state() / "registry.json", data.dump(), true);
}
int main() {
    auto run = fs::current_path() / "inbox-test-data" / random_id();
    fs::create_directories(run);
    try {
        Core core(run / "state");
        for (const auto &id : {"team", "other"}) {
            fs::create_directories(run / id);
            core.request("POST", "/api/projects",
                         {{"id", id},
                          {"name", id},
                          {"mode", "backup"},
                          {"root", path_string(run / id)},
                          {"backup_interval_minutes", 0}});
        }
        auto registry = load(core);
        registry["network"] = {
            {"profile", {{"address", "10.77.0.1"}}},
            {"peers",
             {{"remote", {{"projects", {"team"}}, {"profile", {{"address", "10.77.0.2"}}}}}}}};
        save(core, registry);
        check(act(core, "received-files").at("files").empty() &&
                  !fs::exists(core.state() / "inbox"),
              "An empty listing creates no directory");
        std::string bytes("binary\0payload\r\n", 16);
        Json input = {{"project", "team"}, {"peer", "remote"},     {"id", "first"},
                      {"file", true},      {"name", "Résumé.bin"}, {"sha256", sha256(bytes)}};
        core.direct_receive(input, "10.77.0.2", bytes);
        auto list = act(core, "received-files");
        check(list.at("files").size() == 1 && list.at("errors").empty() &&
                  list.at("unindexed").empty(),
              "Incoming files have persistent receipts");
        auto receipt = list.at("files")[0];
        check(receipt.at("name") == "Résumé.bin" && receipt.at("available") &&
                  !receipt.contains("path"),
              "Listing exposes availability and Unicode metadata without stored paths");
        check(act(core, "received-verify", {{"transfer", "first"}}).at("ok"),
              "SHA-256 verification succeeds");
        auto exported = act(core, "received-export",
                            {{"transfer", "first"}, {"destination", path_string(run / "export")}});
        auto payload = core.state() / "inbox/team/first.received";
        check(read_file(payload) == bytes &&
                  read_file(fs::u8path(exported.at("path").get<std::string>())) == bytes,
              "Export preserves binary bytes and the received source");
        check(Json::parse(read_file(run / "export/receipt.json")).at("sha256") == sha256(bytes),
              "Export includes its integrity receipt");
        for (const auto &destination :
             {run / "export", run / "team/new", run / "other/new", core.state() / "new", run})
            rejects(
                [&] {
                    act(core, "received-export",
                        {{"transfer", "first"}, {"destination", path_string(destination)}});
                },
                "Overlapping destinations are refused");
        check(act(core, "received-files", Json::object(), "other").at("files").empty(),
              "Catalogs are project isolated");
        rejects([&] { act(core, "received-verify", {{"transfer", "../first"}}); },
                "IDs cannot traverse paths");
        check(core.direct_receive(input, "10.77.0.2", bytes).at("duplicate"),
              "Exact replays are idempotent");
        auto changed = input;
        changed["name"] = "changed.bin";
        rejects([&] { core.direct_receive(changed, "10.77.0.2", bytes); },
                "A replay cannot alter its receipt");
        registry = load(core);
        registry["messages"] = Json::array();
        save(core, registry);
        check(core.direct_receive(input, "10.77.0.2", bytes).at("duplicate"),
              "File idempotency survives message history retention");
        Core second(core.state());
        check(act(second, "received-files").at("files").size() == 1,
              "A reloaded core retains its catalog");
        for (const auto &name : {"../escape", "CON.txt", "bad:name", ".", "..", "trailing.",
                                 "slash/name", "back\\name", "bad\nname"}) {
            auto invalid = input;
            invalid["id"] = "invalid";
            invalid["name"] = name;
            rejects([&] { core.direct_receive(invalid, "10.77.0.2", bytes); },
                    "Unportable names are refused");
        }
        auto invalid = input;
        invalid["id"] = "invalid";
        invalid["name"] = std::string("\xff.bin", 5);
        rejects([&] { core.direct_receive(invalid, "10.77.0.2", bytes); },
                "Invalid UTF-8 is refused");
        check(!fs::exists(core.state() / "inbox/team/invalid.received"),
              "Invalid names leave no payload");
        atomic_write(payload, std::string(bytes.size(), 'x'), true);
        check(act(core, "received-files").at("files")[0].at("available"),
              "Listing does not claim to hash available files");
        rejects([&] { act(core, "received-verify", {{"transfer", "first"}}); },
                "Same-size corruption is detected");
        rejects(
            [&] {
                act(core, "received-export",
                    {{"transfer", "first"}, {"destination", path_string(run / "corrupt-export")}});
            },
            "Corrupt files cannot be exported");
        check(!fs::exists(run / "corrupt-export"), "Integrity failure creates no destination");
        rejects([&] { core.direct_receive(input, "10.77.0.2", bytes); },
                "Replay detects local corruption");
        atomic_write(payload, bytes, true);
        atomic_write(core.state() / "inbox/team/orphan.received", "preserved", true);
        check(act(core, "received-files").at("unindexed").size() == 1,
              "Unindexed payloads remain visible");
        rejects(
            [&] {
                act(core, "received-export",
                    {{"transfer", "orphan"}, {"destination", path_string(run / "orphan-export")}});
            },
            "An unindexed file cannot bypass verification");
        registry = load(core);
        registry["received_files"]["team"]["first"]["path"] = path_string(run / "outside");
        save(core, registry);
        check(act(core, "received-verify", {{"transfer", "first"}}).at("ok"),
              "Forged source paths are ignored");
        registry["received_files"]["team"]["broken"] = {{"id", "../escape"}};
        save(core, registry);
        check(act(core, "received-files").at("errors").size() == 1,
              "Malformed receipts are reported individually");
        registry["received_files"]["team"] = Json::object();
        receipt.erase("available");
        registry["messages"] = {{{"id", "first"},
                                 {"project", "team"},
                                 {"peer", "remote"},
                                 {"created", receipt.at("created")},
                                 {"kind", "file"},
                                 {"direction", "in"},
                                 {"body", receipt}}};
        save(core, registry);
        check(act(core, "received-verify", {{"transfer", "first"}}).at("ok"),
              "Legacy 0.7 receipts remain readable");
        auto empty = input;
        empty["id"] = "empty";
        empty["name"] = "receipt.json";
        empty["sha256"] = sha256("");
        core.direct_receive(empty, "10.77.0.2", "");
        registry = load(core);
        registry["messages"] = Json::array();
        save(core, registry);
        check(act(core, "received-files").at("files").size() == 2,
              "Receiving migrates retained legacy receipts");
        exported = act(core, "received-export",
                       {{"transfer", "empty"}, {"destination", path_string(run / "empty-export")}});
        check(fs::file_size(fs::u8path(exported.at("path").get<std::string>())) == 0 &&
                  Json::parse(read_file(run / "empty-export/receipt.json")).at("name") ==
                      "receipt.json",
              "An empty file cannot collide with export metadata");
        HttpServer server(core);
        int port = server.bind("127.0.0.1", 0);
        std::thread listener([&] { server.listen(); });
        try {
            httplib::Client client("127.0.0.1", port);
            auto response =
                client.Post("/api/projects/team/received-files", "{}", "application/json");
            check(response && response->status == 401, "HTTP requires authentication");
            httplib::Headers headers{{"Authorization", "Bearer " + core.api_token()}};
            response =
                client.Post("/api/projects/team/received-files", headers, "{}", "application/json");
            check(response && response->status == 200 &&
                      Json::parse(response->body).at("files").size() == 2,
                  "Authenticated HTTP lists received files");
            auto data =
                Json{{"transfer", "first"}, {"destination", path_string(run / "http-export")}};
            response = client.Post("/api/projects/team/received-export", headers, data.dump(),
                                   "application/json");
            check(response && response->status == 200 &&
                      read_file(run / fs::u8path("http-export/payload/Résumé.bin")) == bytes,
                  "HTTP exports the same verified binary payload");
            server.stop();
            listener.join();
        } catch (...) {
            server.stop();
            listener.join();
            throw;
        }
        check(cli(core, {"received", "list", "team"}).at("files").size() == 2 &&
                  cli(core, {"received", "verify", "team", "first"}).at("ok"),
              "CLI shares the inbox implementation");
        fs::remove(payload);
        bool missing = false;
        auto missingList = act(core, "received-files");
        for (const auto &file : missingList.at("files"))
            if (file.at("id") == "first")
                missing = !file.at("available").get<bool>();
        check(missing, "Missing payloads are unavailable");
        rejects([&] { act(core, "received-verify", {{"transfer", "first"}}); },
                "Missing files cannot verify");
        std::cout << "PASS " << checks << " inbox checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
