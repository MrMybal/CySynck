#include <cysynck/core.hpp>
#include <httplib.h>
#include <atomic>
#include <functional>
#include <fstream>
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
static Json registry(Core &core) { return Json::parse(read_file(core.state() / "registry.json")); }
static void save(Core &core, const Json &data) {
    atomic_write(core.state() / "registry.json", data.dump(), true);
}
static void setup(Core &core, const fs::path &root, const std::string &self,
                  const std::string &peer, const std::string &address = "10.77.0.2") {
    fs::create_directories(root);
    core.request("POST", "/api/projects",
                 {{"id", "team"},
                  {"name", "Team"},
                  {"root", path_string(root)},
                  {"mode", "backup"},
                  {"backup_interval_minutes", 0}});
    auto data = registry(core);
    data["peer_id"] = self;
    data["network"] = {
        {"profile", {{"address", "127.0.0.1"}}},
        {"peers",
         {{peer,
           {{"projects", {"team"}}, {"profile", {{"address", address}, {"mesh_port", 1}}}}}}}};
    save(core, data);
}
static Json begin(const std::string &id, const std::string &bytes,
                  const std::string &peer = "remote") {
    return {{"transfer_action", "begin"},
            {"protocol", 1},
            {"project", "team"},
            {"peer", peer},
            {"id", id},
            {"name", "Résumé.bin"},
            {"bytes", bytes.size()},
            {"sha256", sha256(bytes)}};
}
static Json chunk(const std::string &id, size_t index, const std::string &bytes,
                  const std::string &peer = "remote") {
    return {{"transfer_action", "chunk"},
            {"project", "team"},
            {"peer", peer},
            {"id", id},
            {"index", index},
            {"chunk_sha256", sha256(bytes)}};
}
static Json finish(const std::string &id, const std::string &peer = "remote") {
    return {{"transfer_action", "finish"}, {"project", "team"}, {"peer", peer}, {"id", id}};
}
static Json list(Core &core) { return cli(core, {"transfers", "list", "team"}); }
int main() {
    auto run = fs::current_path() / "transfer-test-data" / random_id();
    fs::create_directories(run);
    constexpr size_t blockSize = 1024 * 1024;
    const std::string block(blockSize, '\x89'), tail("\0tail\xff", 6), payload = block + tail;
    try {
        Core core(run / "receiver");
        setup(core, run / "project", "local", "remote");
        auto limits = cli(core, {"transfers", "limits"});
        check(limits.at("file_limit_mib") == 1024 && limits.at("inbox_quota_mib") == 4096,
              "Old profiles receive bounded large-file defaults");
        for (const auto &bad :
             {Json(-1), Json(0), Json(4097), Json(1.5), Json("1024"), Json(uint64_t(-1))})
            rejects(
                [&] {
                    core.request("POST", "/api/network/transfer-configure",
                                 {{"file_limit_mib", bad}});
                },
                "Invalid file limits are rejected without integer overflow");
        rejects(
            [&] {
                core.request("POST", "/api/network/transfer-configure",
                             {{"inbox_quota_mib", 65537}});
            },
            "Quota maximum is enforced");
        rejects(
            [&] { core.request("POST", "/api/network/transfer-configure", {{"unexpected", 10}}); },
            "Unknown limit fields are refused");
        rejects([&] { cli(core, {"transfers", "configure", "--file-limit-mib", "1.5"}); },
                "CLI accepts only whole MiB");
        rejects([&] { cli(core, {"transfers", "configure", "--inbox-quota-mib", "1"}); },
                "Each quota must cover the configured file limit");
        cli(core,
            {"transfers", "configure", "--file-limit-mib", "4096", "--inbox-quota-mib", "8192"});
        auto boundary = begin("boundary", "");
        boundary["bytes"] = 4096ull * blockSize;
        check(core.direct_receive(boundary, "10.77.0.2").at("next") == 0 &&
                  cli(core, {"transfers", "limits"}).at("reserved_incoming_bytes") ==
                      4096ull * blockSize,
              "4 GiB reservation uses 64-bit arithmetic without allocating the payload");
        auto beyond = boundary;
        beyond["id"] = "beyond";
        beyond["bytes"] = 4096ull * blockSize + 1;
        rejects([&] { core.direct_receive(beyond, "10.77.0.2"); }, "Hard file maximum is enforced");
        rejects(
            [&] {
                cli(core, {"transfers", "configure", "--file-limit-mib", "1024",
                           "--inbox-quota-mib", "1024"});
            },
            "Quotas cannot discard current reservations");
        cli(core, {"transfers", "cancel", "team", "boundary", "--direction", "in"});
        cli(core,
            {"transfers", "configure", "--file-limit-mib", "1024", "--inbox-quota-mib", "4096"});
        auto start = begin("first", payload);
        rejects([&] { core.direct_receive(start, "10.77.0.3"); }, "VPN source is pinned");
        check(core.direct_receive(start, "10.77.0.2").at("next") == 0,
              "Begin reserves an empty journal");
        check(list(core).at("files")[0].at("acknowledged_bytes") == 0,
              "CLI lists pending transfer");
        auto changed = start;
        changed["name"] = "different.bin";
        rejects([&] { core.direct_receive(changed, "10.77.0.2"); }, "ID cannot change metadata");
        changed = start;
        changed["name"] = "../bad";
        changed["id"] = "invalid";
        rejects([&] { core.direct_receive(changed, "10.77.0.2"); }, "Portable names required");
        changed = start;
        changed["id"] = "over-limit";
        changed["bytes"] = 1024ull * blockSize + 1;
        rejects([&] { core.direct_receive(changed, "10.77.0.2"); }, "Per-file size is bounded");
        rejects([&] { core.direct_receive(finish("first"), "10.77.0.2"); },
                "Incomplete file cannot finish");
        rejects([&] { core.direct_receive(chunk("first", 1, tail), "10.77.0.2", tail); },
                "Chunks must arrive in order");
        auto wrong = chunk("first", 0, block);
        wrong["chunk_sha256"] = sha256("wrong");
        rejects([&] { core.direct_receive(wrong, "10.77.0.2", block); },
                "Wrong digest is rejected");
        rejects([&] { core.direct_receive(chunk("first", 0, tail), "10.77.0.2", tail); },
                "Short block is rejected");
        check(core.direct_receive(chunk("first", 0, block), "10.77.0.2", block).at("next") == 1,
              "First block is journaled");
        cli(core, {"transfers", "configure", "--file-limit-mib", "1"});
        Core restarted(run / "receiver");
        check(restarted.direct_receive(start, "10.77.0.2").at("next") == 1,
              "Restart returns the verified prefix");
        check(restarted.direct_receive(chunk("first", 0, block), "10.77.0.2", block).at("next") ==
                  1,
              "Lost acknowledgement can retry the same block");
        std::string different(blockSize, 'x');
        rejects(
            [&] { restarted.direct_receive(chunk("first", 0, different), "10.77.0.2", different); },
            "Accepted block cannot be changed");
        auto raw = registry(restarted);
        raw["network"]["peers"]["remote"]["projects"] = Json::array();
        save(restarted, raw);
        rejects([&] { restarted.direct_receive(chunk("first", 1, tail), "10.77.0.2", tail); },
                "Revocation prevents continuation");
        raw["network"]["peers"]["remote"]["projects"] = {"team"};
        save(restarted, raw);
        auto blockPath = restarted.state() / "transfers/incoming/team/first/0.block";
        atomic_write(blockPath, different, true);
        rejects([&] { restarted.direct_receive(start, "10.77.0.2"); },
                "Resume verifies persisted blocks");
        atomic_write(blockPath, block, true);
        restarted.direct_receive(chunk("first", 1, tail), "10.77.0.2", tail);
        check(restarted.direct_receive(finish("first"), "10.77.0.2").at("delivered"),
              "Whole digest checked at finish");
        check(read_file(restarted.state() / "inbox/team/first.received", 2 * blockSize) == payload,
              "Binary payload survives chunk assembly");
        check(list(restarted).at("files").empty() && !fs::exists(blockPath),
              "Completed blocks and journal are released");
        check(restarted.direct_receive(start, "10.77.0.2").at("delivered") &&
                  restarted.direct_receive(finish("first"), "10.77.0.2").at("delivered"),
              "Completed replay is idempotent");
        check(cli(restarted, {"received", "verify", "team", "first"}).at("ok"),
              "Inbox accepts new protocol receipts");
        check(cli(restarted, {"transfers", "limits"}).at("file_limit_mib") == 1,
              "Reduced file limit persists and preserves existing transfers and receipts");
        rejects([&] { cli(restarted, {"transfers", "configure", "--inbox-quota-mib", "1"}); },
                "Received payloads prevent quota reduction below usage");
        cli(restarted, {"transfers", "configure", "--file-limit-mib", "1024"});
        auto empty = begin("empty", "");
        restarted.direct_receive(empty, "10.77.0.2");
        check(restarted.direct_receive(finish("empty"), "10.77.0.2").at("delivered"),
              "Empty file finishes without blocks");
        auto badHash = begin("bad-hash", tail);
        badHash["sha256"] = sha256("wrong");
        restarted.direct_receive(badHash, "10.77.0.2");
        restarted.direct_receive(chunk("bad-hash", 0, tail), "10.77.0.2", tail);
        rejects([&] { restarted.direct_receive(finish("bad-hash"), "10.77.0.2"); },
                "Final checksum mismatch cannot publish");
        check(!fs::exists(restarted.state() / "inbox/team/bad-hash.received"),
              "Bad assembly stays outside inbox");
        check(cli(restarted, {"transfers", "cancel", "team", "bad-hash", "--direction", "in"})
                  .at("received_files_preserved"),
              "Cancel deletes only partial storage");
        rejects(
            [&] {
                cli(restarted, {"transfers", "cancel", "team", "../first", "--direction", "in"});
            },
            "Cancel rejects traversal");
        // Crash after payload rename, before receipt persistence: finish validates and adopts it.
        auto crash = begin("publish-crash", tail);
        restarted.direct_receive(crash, "10.77.0.2");
        restarted.direct_receive(chunk("publish-crash", 0, tail), "10.77.0.2", tail);
        atomic_write(restarted.state() / "inbox/team/publish-crash.received", tail, true);
        check(restarted.direct_receive(finish("publish-crash"), "10.77.0.2").at("delivered"),
              "Publish crash recovers exactly once");
        auto cleanup = begin("cleanup", tail);
        restarted.direct_receive(cleanup, "10.77.0.2");
        restarted.direct_receive(chunk("cleanup", 0, tail), "10.77.0.2", tail);
        auto unexpected = restarted.state() / "transfers/incoming/team/cleanup/keep.txt";
        atomic_write(unexpected, "preserve this");
        auto cleanupResult = restarted.direct_receive(finish("cleanup"), "10.77.0.2");
        check(cleanupResult.at("delivered") && cleanupResult.at("cleanup_pending"),
              "Cleanup failure does not hide successful delivery");
        rejects(
            [&] {
                cli(restarted, {"transfers", "cancel", "team", "cleanup", "--direction", "in"});
            },
            "Cleanup cannot recursively remove unexpected files");
        check(read_file(unexpected) == "preserve this", "Unexpected local content is preserved");
        fs::remove(unexpected);
        // Windows can retain a deleted file briefly while another process inspects it.
        auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            cleanupResult = restarted.direct_receive(cleanup, "10.77.0.2");
            if (!cleanupResult.value("cleanup_pending", false))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < cleanupDeadline);
        check(cleanupResult.at("delivered") && !cleanupResult.value("cleanup_pending", false) &&
                  list(restarted).at("files").empty(),
              "Delivered replay retries safe cleanup without another receipt");
        fs::create_directories(run / "other-project");
        restarted.request("POST", "/api/projects",
                          {{"id", "other"},
                           {"name", "Other"},
                           {"mode", "backup"},
                           {"root", path_string(run / "other-project")},
                           {"backup_interval_minutes", 0}});
        auto isolated = begin("isolated", tail);
        isolated["project"] = "other";
        rejects([&] { restarted.direct_receive(isolated, "10.77.0.2"); },
                "Peer trust is scoped to its project");
        // Reservations, including legacy receives, share the inbox quota without allocating 1 GiB.
        cli(restarted,
            {"transfers", "configure", "--file-limit-mib", "100", "--inbox-quota-mib", "1024"});
        for (int i = 0; i < 10; ++i) {
            auto reservation = begin("reserved-" + std::to_string(i), "");
            reservation["bytes"] = 100 * blockSize;
            restarted.direct_receive(reservation, "10.77.0.2");
        }
        auto excess = begin("excess", "");
        excess["bytes"] = 25 * blockSize;
        rejects([&] { restarted.direct_receive(excess, "10.77.0.2"); },
                "Reservations cannot exceed the inbox quota");
        Json legacy{{"project", "team"},    {"peer", "remote"},
                    {"id", "legacy"},       {"file", true},
                    {"name", "legacy.bin"}, {"sha256", sha256(std::string(25 * blockSize, 'x'))}};
        rejects(
            [&] {
                restarted.direct_receive(legacy, "10.77.0.2", std::string(25 * blockSize, 'x'));
            },
            "Legacy receives account for partial reservations");
        for (int i = 0; i < 10; ++i)
            cli(restarted, {"transfers", "cancel", "team", "reserved-" + std::to_string(i),
                            "--direction", "in"});
        cli(restarted,
            {"transfers", "configure", "--file-limit-mib", "1024", "--inbox-quota-mib", "4096"});
        for (int i = 0; i < 32; ++i)
            restarted.direct_receive(begin("bounded-" + std::to_string(i), ""), "10.77.0.2");
        rejects([&] { restarted.direct_receive(begin("too-many", ""), "10.77.0.2"); },
                "Even empty partials are bounded");
        for (int i = 0; i < 32; ++i)
            cli(restarted, {"transfers", "cancel", "team", "bounded-" + std::to_string(i),
                            "--direction", "in"});
        // Actual sender: receiver accepts block 1 but its response is lost. Resume skips it.
#ifdef _WIN32
        _putenv_s("CYSYNCK_MESH_TOKEN", "test-only-mesh-secret-"
                                        "1234567890");
#else
        setenv("CYSYNCK_MESH_TOKEN",
               "test-only-mesh-secret-"
               "1234567890",
               1);
#endif
        Core sender(run / "sender"), receiver(run / "network-receiver");
        setup(sender, run / "sender-project", "sender", "receiver", "127.0.0.1");
        setup(receiver, run / "receiver-project", "receiver", "sender", "127.0.0.1");
        // Stream a real payload beyond the former 100 MiB cap; keep test memory bounded.
        auto networkFile = run / "sender-project/network.bin";
        {
            std::ofstream output(networkFile, std::ios::binary);
            for (int i = 0; i < 101; ++i)
                output.write(block.data(), block.size());
            output.write(tail.data(), tail.size());
        }
        auto networkHash = sha256_file(networkFile);
        constexpr int networkChunks = 102;
        cli(sender, {"transfers", "configure", "--file-limit-mib", "1"});
        rejects([&] { cli(sender, {"send-file", "team", "receiver", "network.bin"}); },
                "Oversized source fails before publishing an outgoing journal");
        check(list(sender).at("files").empty(), "Failed capture does not publish a journal");
        cli(sender, {"transfers", "configure", "--file-limit-mib", "1024"});
        httplib::Server proxy;
        std::atomic<int> sentBlocks = 0;
        std::atomic<bool> interrupt = true;
        std::atomic<bool> dropFinish = false, oldPeer = false;
        proxy.Post("/transfer/(begin|chunk|finish)", [&](const httplib::Request &req,
                                                         httplib::Response &res) {
            try {
                if (oldPeer) {
                    res.status = 404;
                    return;
                }
                Json input;
                bool isChunk = req.path == "/transfer/chunk";
                if (isChunk) {
                    input = chunk(req.get_header_value("X-CySynck-Id"),
                                  std::stoul(req.get_header_value("X-CySynck-Chunk")), req.body,
                                  "sender");
                    input["chunk_sha256"] = req.get_header_value("X-CySynck-Chunk-SHA256");
                    ++sentBlocks;
                } else
                    input = Json::parse(req.body);
                input["peer"] = req.get_header_value("X-CySynck-Peer");
                input["transfer_action"] = req.path.substr(10);
                auto result =
                    receiver.direct_receive(input, req.remote_addr, isChunk ? req.body : "");
                if (isChunk && input.at("index") == 1 && interrupt.exchange(false)) {
                    res.status = 503;
                    return;
                }
                if (req.path == "/transfer/finish" && dropFinish.exchange(false)) {
                    res.status = 503;
                    return;
                }
                res.set_content(result.dump(), "application/json");
            } catch (...) {
                res.status = 400;
            }
        });
        int port = proxy.bind_to_any_port("127.0.0.1");
        check(port > 0, "Test peer bound");
        raw = registry(sender);
        raw["network"]["peers"]["receiver"]["profile"]["mesh_port"] = port;
        save(sender, raw);
        std::thread listener([&] { proxy.listen_after_bind(); });
        try {
            auto result = cli(sender, {"send-file", "team", "receiver", "network.bin"});
            auto id = result.at("id").get<std::string>();
            check(!result.at("delivered").get<bool>() && result.at("next") == 1 && sentBlocks == 2,
                  "Failed acknowledgement keeps the outgoing transfer ID");
            check(list(receiver).at("files")[0].at("acknowledged_bytes") == 2 * blockSize,
                  "Receiver can be ahead of the sender after a lost response");
            check(!fs::exists(sender.state() / "objects") ||
                      fs::is_empty(sender.state() / "objects"),
                  "Transfer capture creates one frozen copy without a backup object");
            rejects(
                [&] {
                    cli(sender, {"transfers", "configure", "--file-limit-mib", "1",
                                 "--outbox-quota-mib", "100"});
                },
                "Outgoing snapshot reservations prevent quota reduction");
            atomic_write(run / "sender-project/network.bin", "changed after interruption");
            Core senderRestarted(run / "sender");
            result = cli(senderRestarted, {"transfers", "resume", "team", id});
            check(result.at("delivered") && sentBlocks == networkChunks,
                  "Resume sends only missing chunks");
            auto receivedFile = receiver.state() / ("inbox/team/" + id + ".received");
            check(fs::file_size(receivedFile) == 101ull * blockSize + tail.size() &&
                      sha256_file(receivedFile) == networkHash,
                  "Resume uses the frozen snapshot after source changes");
            auto exported = cli(
                receiver, {"received", "export", "team", id, path_string(run / "large-export")});
            check(exported.at("exported") &&
                      sha256_file(fs::u8path(exported.at("path").get<std::string>())) ==
                          networkHash,
                  "Large received payload exports with full-file integrity verification");
            check(!fs::exists(sender.state() / ("transfers/outgoing/team/" + id + "/snapshot")),
                  "Delivery frees outgoing snapshot");
            check(cli(senderRestarted, {"transfers", "resume", "team", id}).at("delivered") &&
                      sentBlocks == networkChunks,
                  "Resuming delivered transfer does not resend");
            check(senderRestarted.request("POST", "/api/network/history", {{"project", "team"}})
                          .size() == 1,
                  "Sent history records delivery once");
            cli(senderRestarted, {"transfers", "cancel", "team", id, "--direction", "out"});
            check(list(senderRestarted).at("files").empty(),
                  "Delivered history entry can be removed locally");
            atomic_write(run / "sender-project/empty.bin", "");
            dropFinish = true;
            auto emptyResult = cli(senderRestarted, {"send-file", "team", "receiver", "empty.bin"});
            auto emptyId = emptyResult.at("id").get<std::string>();
            check(!emptyResult.at("delivered").get<bool>(),
                  "Lost final response keeps transfer resumable");
            check(cli(senderRestarted, {"transfers", "resume", "team", emptyId}).at("delivered") &&
                      sentBlocks == networkChunks,
                  "Resume reconciles a delivery whose final response was lost");
            oldPeer = true;
            auto oldResult = cli(senderRestarted, {"send-file", "team", "receiver", "empty.bin"});
            check(!oldResult.at("delivered").get<bool>() &&
                      oldResult.at("error_code") == "unsupported_protocol" &&
                      oldResult.at("error").get<std::string>().find("--legacy true") !=
                          std::string::npos,
                  "Old peer gives an explicit legacy instruction without silent fallback");
            auto pendingId = oldResult.at("id").get<std::string>();
            auto stored = registry(senderRestarted);
            stored["transfers_out"]["team"][pendingId]["next"] = -1;
            save(senderRestarted, stored);
            check(list(senderRestarted).at("errors").size() == 1,
                  "Malformed progress is isolated in transfer listing");
            rejects([&] { cli(senderRestarted, {"transfers", "resume", "team", pendingId}); },
                    "Malformed progress cannot resume");
            stored["transfers_out"]["team"][pendingId]["next"] = 0;
            save(senderRestarted, stored);
            {
                DiskLock sending(senderRestarted.state(), "transfers-send.lock", false);
                rejects(
                    [&] {
                        cli(senderRestarted,
                            {"transfers", "cancel", "team", pendingId, "--direction", "out"});
                    },
                    "Removal cannot race an active sender");
            }
            cli(senderRestarted, {"transfers", "cancel", "team", pendingId, "--direction", "out"});
            check(!fs::exists(senderRestarted.state() /
                              ("transfers/outgoing/team/" + pendingId + "/snapshot")),
                  "Cancel frees the frozen outgoing source");
            proxy.stop();
            listener.join();
        } catch (...) {
            proxy.stop();
            listener.join();
            throw;
        }
        // Production HTTP routes enforce the secret and pinned address, and still accept /file.
        HttpServer server(receiver, true);
        port = server.bind("127.0.0.1", 0);
        std::thread realListener([&] { server.listen(); });
        try {
            httplib::Client client("127.0.0.1", port);
            auto input = begin("http", tail, "sender");
            auto response = client.Post("/transfer/begin", input.dump(), "application/json");
            check(response && response->status == 401, "Resumable HTTP requires the mesh secret");
            httplib::Headers headers{{"Authorization", "Bearer " + env("CYSYNCK_MESH_TOKEN")},
                                     {"X-CySynck-Peer", "sender"}};
            response = client.Post("/transfer/begin", headers, input.dump(), "application/json");
            check(response && response->status == 200, "HTTP begin is routed");
            auto crossOrigin = headers;
            crossOrigin.emplace("Origin", "http://untrusted.example");
            response =
                client.Post("/transfer/begin", crossOrigin, input.dump(), "application/json");
            check(response && response->status == 403, "Cross-origin peer requests are refused");
            auto chunkHeaders = headers;
            chunkHeaders.emplace("X-CySynck-Project", "team");
            chunkHeaders.emplace("X-CySynck-Id", "http");
            chunkHeaders.emplace("X-CySynck-Chunk", "0");
            chunkHeaders.emplace("X-CySynck-Chunk-SHA256", sha256(tail));
            response =
                client.Post("/transfer/chunk", chunkHeaders, tail, "application/octet-stream");
            check(response && response->status == 200, "HTTP chunk preserves binary bytes");
            response = client.Post("/transfer/finish", headers, finish("http", "sender").dump(),
                                   "application/json");
            check(response && response->status == 200 &&
                      Json::parse(response->body).at("delivered"),
                  "HTTP finish publishes receipt");
            headers.emplace("X-CySynck-Project", "team");
            headers.emplace("X-CySynck-Id", "old-client");
            headers.emplace("X-CySynck-SHA256", sha256(tail));
            headers.emplace("X-CySynck-Name", "old.bin");
            response = client.Post("/file", headers, tail, "application/octet-stream");
            check(response && response->status == 200, "Legacy file endpoint remains compatible");
            raw = registry(sender);
            raw["network"]["peers"]["receiver"]["profile"]["mesh_port"] = port;
            save(sender, raw);
            auto legacyResult =
                cli(sender, {"send-file", "team", "receiver", "network.bin", "--legacy", "true"});
            check(legacyResult.at("delivered"),
                  "Explicit legacy sender works with production HTTP receiver");
            server.stop();
            realListener.join();
        } catch (...) {
            server.stop();
            realListener.join();
            throw;
        }
        std::cout << "PASS " << checks << " transfer checks; fixtures: " << path_string(run)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
