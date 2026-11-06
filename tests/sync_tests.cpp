#include <cysynck/core.hpp>
#include <httplib.h>
#include <iostream>
#include <set>
#include <thread>
#include <functional>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

using namespace cysynck;
static int checks = 0;
static void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &action, const std::string &message) {
    try {
        action();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
static int free_port() {
    static std::set<int> used;
    for (int retry = 0; retry < 30; ++retry) {
        auto socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket == INVALID_SOCKET)
            throw std::runtime_error("Cannot allocate test port");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            httplib::detail::close_socket(socket);
            throw std::runtime_error("Cannot bind a test port");
        }
        socklen_t size = sizeof(address);
        getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size);
        auto port = ntohs(address.sin_port);
        httplib::detail::close_socket(socket);
        if (used.insert(port).second)
            return port;
    }
    throw std::runtime_error("Cannot choose unique test ports");
}
static Json api(Core &core, const std::string &method, const std::string &path,
                const Json &data = Json::object()) {
    auto config = core.request("GET", "/api/engine/status").at("config");
    httplib::Client client(config.at("url").get<std::string>());
    client.set_connection_timeout(1);
    client.set_read_timeout(5);
    httplib::Headers headers{{"X-API-Key", read_file(core.state() / "engine/api-key", 4096)}};
    auto endpoint = "/rest/" + path;
    httplib::Result response;
    if (method == "GET")
        response = client.Get(endpoint, headers);
    else if (method == "PATCH")
        response = client.Patch(endpoint, headers, data.dump(), "application/json");
    else
        response = client.Post(endpoint, headers, data.dump(), "application/json");
    if (!response || response->status >= 300)
        throw std::runtime_error(
            "Fixture API failed: " + path +
            (response ? " HTTP " + std::to_string(response->status) : " (unreachable)"));
    return response->body.empty() ? Json::object() : Json::parse(response->body);
}
static Json engine(Core &core, const std::string &action, const Json &data = Json::object()) {
    return core.request(action == "status" || action == "devices" ? "GET" : "POST",
                        "/api/engine/" + action, data);
}
static Json add_project(Core &core, const std::string &id, const fs::path &root,
                        const std::string &mode = "sendreceive", const fs::path &exchange = {}) {
    fs::create_directories(root);
    return core.request("POST", "/api/projects",
                        {{"id", id},
                         {"name", id},
                         {"root", path_string(root)},
                         {"mode", mode},
                         {"exchange_path", exchange.empty() ? "" : path_string(exchange)},
                         {"backup_interval_minutes", 0}});
}
static Json action(Core &core, const std::string &project, const std::string &name,
                   const Json &data = Json::object()) {
    try {
        return core.request("POST", "/api/projects/" + project + "/" + name, data);
    } catch (const std::exception &error) {
        // Fixture diagnostics do not contain credentials or raw engine output.
        throw std::runtime_error("Project " + project + " action " + name + ": " + error.what());
    }
}
static void eventually(const std::function<bool()> &condition, const std::string &message,
                       std::chrono::seconds timeout = std::chrono::seconds(25)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::string last_error;
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (condition()) {
                ++checks;
                return;
            }
        } catch (const std::exception &error) {
            last_error = error.what();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    throw std::runtime_error(message + (last_error.empty() ? "" : ": " + last_error));
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Pass the path of a Syncthing executable\n";
        return 1;
    }
    auto root = fs::current_path() / "real-sync-data" / random_id();
    fs::create_directories(root);
    try {
        check(fs::space(root).available >= 256ull * 1024 * 1024,
              "Real-engine fixtures require at least 256 MiB of free disk space");
        auto executable = path_string(fs::canonical(fs::u8path(argv[1])));
        Core a(root / "node-a"), b(root / "node-b");
        int guiA = free_port(), guiB = free_port(), syncA = free_port(), syncB = free_port();
        engine(a, "configure",
               {{"executable", executable},
                {"port", guiA},
                {"listen_port", syncA},
                {"loopback_only", true},
                {"auto_start", true}});
        engine(b, "configure",
               {{"executable", executable},
                {"port", guiB},
                {"listen_port", syncB},
                {"loopback_only", true}});
        auto statusA = engine(a, "start"), statusB = engine(b, "start");
        // Small isolated fixtures need an absolute reserve: 1% of a multi-TB developer
        // disk can exceed its available space even when these tests fit comfortably.
        // Only these temporary engines are changed; keep a nonzero safety reserve.
        for (auto node : {&a, &b}) {
            api(*node, "PATCH", "config/options",
                {{"minHomeDiskFree", {{"value", 128}, {"unit", "MB"}}}});
            api(*node, "PATCH", "config/defaults/folder",
                {{"minDiskFree", {{"value", 128}, {"unit", "MB"}}}});
        }
        check(statusA["owned"].get<bool>() && statusB["owned"].get<bool>(),
              "Both nodes must own their exact child");
        check(statusA["device_id"] != statusB["device_id"],
              "Isolated engines have separate device identities");
        check(api(a, "GET", "config/folders").empty() && api(b, "GET", "config/folders").empty(),
              "No default personal folders are created");
        check(statusA.dump().find("apikey") == std::string::npos,
              "Public status must not contain the API key");
        std::cout << "PASS two isolated engines started without personal folders\n";
        {
            Core observer(a.state());
            auto attached = engine(observer, "start");
            check(attached["running"].get<bool>() && !attached["owned"].get<bool>(),
                  "A second controller attaches without stealing ownership");
            rejects([&] { engine(observer, "stop"); }, "A non-owner must not stop an engine");
            rejects(
                [&] {
                    engine(observer, "configure",
                           {{"executable", executable},
                            {"port", free_port()},
                            {"listen_port", free_port()}});
                },
                "Configuration cannot change while another controller owns the engine");
        }
        check(engine(a, "status")["running"].get<bool>(),
              "Observer destruction must preserve the owner's engine");
        auto peerA = statusA.at("device_id").get<std::string>(),
             peerB = statusB.at("device_id").get<std::string>();
        engine(a, "add-device",
               {{"device_id", peerB}, {"address", "tcp://127.0.0.1:" + std::to_string(syncB)}});
        engine(b, "add-device",
               {{"device_id", peerA}, {"address", "tcp://127.0.0.1:" + std::to_string(syncA)}});
        auto folderA = root / "files-a", folderB = root / "files-b";
        add_project(a, "collaboration", folderA);
        add_project(b, "collaboration", folderB);
        action(a, "collaboration", "register");
        action(b, "collaboration", "register");
        engine(a, "share", {{"project", "collaboration"}, {"device_id", peerB}});
        engine(b, "share", {{"project", "collaboration"}, {"device_id", peerA}});
        std::string bytes(2 * 1024 * 1024, '\0');
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = static_cast<char>((i * 17) % 251);
        atomic_write(folderA / "payload.bin", bytes);
        atomic_write(folderA / "notes.txt", "alpha\n");
        atomic_write(folderA / fs::u8path("dossier/été.txt"), "Bonjour\n");
        action(a, "collaboration", "sync");
        eventually(
            [&] {
                return fs::exists(folderB / "payload.bin") &&
                       sha256_file(folderB / "payload.bin") == sha256(bytes) &&
                       fs::exists(folderB / fs::u8path("dossier/été.txt")) &&
                       read_file(folderB / "notes.txt") == "alpha\n";
            },
            "Initial real file transfer did not complete");
        atomic_write(folderB / "notes.txt", "beta\n");
        action(b, "collaboration", "sync");
        eventually([&] { return read_file(folderA / "notes.txt") == "beta\n"; },
                   "Reverse synchronization did not complete");
        fs::remove(folderA / "notes.txt");
        action(a, "collaboration", "sync");
        eventually([&] { return !fs::exists(folderB / "notes.txt"); },
                   "Deletion did not propagate");
        check(fs::exists(folderB / ".stversions"),
              "Syncthing remote versioning must retain replaced/deleted files");
        action(b, "collaboration", "pause", {{"paused", true}});
        check(api(b, "GET", "config/folders/cysynck-collaboration").at("paused").get<bool>(),
              "Pause reaches the real engine");
        atomic_write(folderA / "after-pause.txt", "resume\n");
        action(a, "collaboration", "sync");
        action(b, "collaboration", "pause", {{"paused", false}});
        eventually([&] { return read_file(folderB / "after-pause.txt") == "resume\n"; },
                   "Transfer did not resume after pause");
        std::cout << "PASS real text, binary, Unicode, reverse changes, deletions, versioning and "
                     "pause/resume\n";
        atomic_write(folderA / "simultaneous.txt", "base\n");
        action(a, "collaboration", "sync");
        eventually([&] { return read_file(folderB / "simultaneous.txt") == "base\n"; },
                   "Conflict baseline did not synchronize");
        api(a, "PATCH", "config/devices/" + peerB, {{"paused", true}});
        api(b, "PATCH", "config/devices/" + peerA, {{"paused", true}});
        eventually(
            [&] {
                return !api(a, "GET", "system/connections")
                            .at("connections")
                            .at(peerB)
                            .value("connected", false) &&
                       !api(b, "GET", "system/connections")
                            .at("connections")
                            .at(peerA)
                            .value("connected", false);
            },
            "Peer connections did not pause for offline edits");
        const std::string editA = "version from A\n", editB = "different version from B\n";
        atomic_write(folderA / "simultaneous.txt", editA);
        atomic_write(folderB / "simultaneous.txt", editB);
        action(a, "collaboration", "sync");
        action(b, "collaboration", "sync");
        eventually(
            [&] {
                auto fileA =
                    api(a, "GET", "db/file?folder=cysynck-collaboration&file=simultaneous.txt");
                auto fileB =
                    api(b, "GET", "db/file?folder=cysynck-collaboration&file=simultaneous.txt");
                return fileA.at("local").at("size") == editA.size() &&
                       fileB.at("local").at("size") == editB.size();
            },
            "Offline versions did not enter both local indexes");
        api(a, "PATCH", "config/devices/" + peerB, {{"paused", false}});
        api(b, "PATCH", "config/devices/" + peerA, {{"paused", false}});
        std::string conflictPath;
        eventually(
            [&] {
                auto found = action(a, "collaboration", "conflicts");
                for (const auto &item : found.at("files"))
                    if (item.at("original") == "simultaneous.txt") {
                        conflictPath = item.at("path");
                        return true;
                    }
                return false;
            },
            "Concurrent real edits did not produce a discoverable conflict copy",
            std::chrono::seconds(60));
        auto conflictReview = action(a, "collaboration", "conflict-diff", {{"path", conflictPath}});
        check(std::set<std::string>{conflictReview.at("before"), conflictReview.at("after")} ==
                  std::set<std::string>{editA, editB},
              "Conflict review preserves both concurrent real versions");
        eventually([&] { return fs::is_regular_file(folderB / fs::u8path(conflictPath)); },
                   "Real conflict copy did not propagate to the other peer");
        std::cout << "PASS concurrent offline edits, real conflict detection and source-preserving "
                     "diff\n";
        auto resolution = Json{{"path", conflictPath}, {"choice", "conflict"}};
        resolution["plan"] = action(a, "collaboration", "resolution-plan", resolution).at("plan");
        rejects([&] { action(a, "collaboration", "resolve-conflict", resolution); },
                "Real active Syncthing project refuses resolution before pause");
        action(a, "collaboration", "pause", {{"paused", true}});
        auto resolved = action(a, "collaboration", "resolve-conflict", resolution);
        check(resolved.at("phase") == "applied" &&
                  read_file(folderA / "simultaneous.txt") ==
                      conflictReview.at("after").get<std::string>(),
              "Paused real Syncthing project applies the selected conflict copy");
        check(api(a, "GET", "db/ignores?folder=cysynck-collaboration").at("ignore").at(0) ==
                  "(?i).cysynck-recovery",
              "Recovery exclusion is installed before files are moved");
        action(a, "collaboration", "pause", {{"paused", false}});
        action(a, "collaboration", "sync");
        eventually(
            [&] {
                return read_file(folderB / "simultaneous.txt") ==
                           conflictReview.at("after").get<std::string>() &&
                       !fs::exists(folderB / fs::u8path(conflictPath));
            },
            "Resolved contents and conflict removal did not reach the other peer");
        check(!fs::exists(folderB / ".cysynck-recovery"),
              "Private recovery files never reach the other real peer");
        std::cout << "PASS real conflict resolution, protected recovery copies and explicit "
                     "synchronization resume\n";
        auto repoA =
                 add_project(a, "git-team", root / "repo-a", "git-bundles", root / "exchange-a"),
             repoB =
                 add_project(b, "git-team", root / "repo-b", "git-bundles", root / "exchange-b");
        for (auto p : {repoA, repoB}) {
            git(p, {"init", "--initial-branch=main"});
            atomic_write(fs::u8path(p["root"].get<std::string>()) / "code.txt",
                         p["root"].get<std::string>());
            git(p, {"add", "code.txt"});
            git(p, {"-c", "user.name=CySynck Test", "-c", "user.email=test@example.invalid",
                    "commit", "-m", "Initial"});
        }
        action(a, "git-team", "register");
        action(b, "git-team", "register");
        engine(a, "share", {{"project", "git-team"}, {"device_id", peerB}});
        engine(b, "share", {{"project", "git-team"}, {"device_id", peerA}});
        auto before = git(repoB, {"rev-parse", "HEAD"});
        std::string asset(1024 * 1024 + 37, '\x89');
        auto assetHash = sha256(asset);
        auto lfsPath = "lfs/objects/" + assetHash.substr(0, 2) + "/" + assetHash.substr(2, 2) +
                       "/" + assetHash;
        auto rootA = fs::u8path(repoA.at("root").get<std::string>());
        atomic_write(rootA / ".git" / lfsPath, asset, true);
        atomic_write(rootA / "Asset.uasset",
                     "version https://git-lfs.github.com/spec/v1\noid sha256:" + assetHash +
                         "\nsize " + std::to_string(asset.size()) + "\n");
        git(repoA, {"add", "Asset.uasset"});
        git(repoA, {"-c", "user.name=CySynck Test", "-c", "user.email=test@example.invalid",
                    "commit", "-m", "Shared LFS asset"});
        auto bundle = action(a, "git-team", "export", {{"lfs", true}});
        action(a, "git-team", "sync");
        eventually(
            [&] {
                auto inbox = action(b, "git-team", "inbox");
                for (auto &item : inbox)
                    if (item["id"] == bundle["id"] &&
                        item.at("lfs_status").at("complete").get<bool>())
                        return true;
                return false;
            },
            "Git bundle did not transfer through real Syncthing");
        auto imported =
            action(b, "git-team", "import", {{"peer", bundle["peer"]}, {"bundle", bundle["id"]}});
        check(git(repoB, {"rev-parse", imported.at("tracking_ref").get<std::string>()}) ==
                  bundle.at("head").get<std::string>(),
              "Tracking branch must contain the transported commit");
        check(git(repoB, {"rev-parse", "HEAD"}) == before,
              "Real Git exchange must preserve the local working branch");
        check(imported.at("lfs").at("verified") &&
                  sha256_file(fs::u8path(repoB.at("root").get<std::string>()) / ".git" / lfsPath) ==
                      assetHash,
              "Real Syncthing transports verified Git LFS assets into the local cache");
        std::cout
            << "PASS Git bundles transported by real Syncthing and imported without checkout\n";
        engine(a, "stop");
        a.tick();
        check(!engine(a, "status")["running"].get<bool>(),
              "Explicit stop must suppress autostart for this controller");
        check(engine(b, "status")["running"].get<bool>(), "Stopping A must leave B running");
        engine(a, "start");
        check(engine(a, "status")["running"].get<bool>(),
              "The isolated engine can restart on the same ports");
        auto pid = engine(a, "status").at("pid").get<uint64_t>();
#ifdef _WIN32
        HANDLE owned = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
        if (!owned)
            throw std::runtime_error("Cannot simulate owned test child crash");
        auto terminated = TerminateProcess(owned, 1);
        CloseHandle(owned);
        if (!terminated)
            throw std::runtime_error("Cannot terminate the owned test child");
#else
        if (kill(static_cast<pid_t>(pid), SIGKILL) != 0)
            throw std::runtime_error("Cannot terminate the owned test child");
#endif
        // Process termination is asynchronous. Poll actual recovery, rather than
        // assuming that a fixed sleep has let the OS finish releasing the child.
        eventually(
            [&] {
                a.tick();
                auto recovered = engine(a, "status");
                return recovered.at("running").get<bool>() && recovered.at("owned").get<bool>() &&
                       recovered.at("pid").get<uint64_t>() != pid;
            },
            "Supervisor restarts its own crashed engine with a new process");
        check(engine(b, "status")["running"].get<bool>(), "Crash recovery must not affect B");
        engine(a, "stop");
        engine(b, "stop");
        std::cout << "PASS ownership protection, stop, restart and crash recovery\nPASS " << checks
                  << " real-engine checks; fixtures: " << path_string(root) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << path_string(root) << '\n';
        return 1;
    }
}
