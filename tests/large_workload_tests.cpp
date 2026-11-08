#include <cysynck/core.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
using namespace cysynck;
namespace {
constexpr uintmax_t mib = 1024ull * 1024, bytes = 2304 * mib + 123;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
uintmax_t peak_memory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    check(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0,
          "Cannot measure workload peak memory");
    return counters.PeakWorkingSetSize;
#else
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "Cannot measure workload peak memory");
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return uintmax_t(usage.ru_maxrss) * 1024;
#endif
#endif
}
struct Receiver {
    std::unique_ptr<HttpServer> server;
    std::jthread listener;
    int start(Core &core, int port = 0) {
        server = std::make_unique<HttpServer>(core, true);
        auto bound = server->bind("127.0.0.1", port);
        listener = std::jthread([this] { server->listen(); });
        return bound;
    }
    void stop() {
        if (server)
            server->stop();
        if (listener.joinable())
            listener.join();
        server.reset();
    }
    ~Receiver() { stop(); }
};
std::string peer_id(Core &core) {
    return Json::parse(read_file(core.state() / "registry.json")).at("peer_id");
}
void network_fixture(Core &core, const std::string &other, int port) {
    auto registry = Json::parse(read_file(core.state() / "registry.json"));
    auto profile = [](const std::string &id, int mesh_port) {
        return Json{{"version", 1},
                    {"peer_id", id},
                    {"name", id},
                    {"address", "127.0.0.1"},
                    {"public_key", std::string(43, 'A') + "="},
                    {"endpoint", ""},
                    {"listen_port", 51820},
                    {"mesh_port", mesh_port},
                    {"device_id", ""}};
    };
    // Protocol workload only: loopback is injected directly into isolated private test
    // state. Production pairing refuses it. Real kernel routing has a separate test.
    registry["network"] = {
        {"profile", profile(registry.at("peer_id"), 8485)},
        {"peers",
         {{other, {{"profile", profile(other, port)}, {"projects", Json::array({"large"})}}}}}};
    atomic_write(core.state() / "registry.json", registry.dump(), true);
    cli(core, {"transfers", "configure", "--file-limit-mib", "3072", "--inbox-quota-mib", "8192",
               "--outbox-quota-mib", "8192"});
}
void project(Core &core, const fs::path &root) {
    fs::create_directories(root);
    cli(core, {"project", "add", "Large workload", path_string(root), "--id", "large", "--mode",
               "backup", "--storage", "cdc", "--interval", "0", "--keep", "10"});
}
void generate(const fs::path &file) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    check(bool(output), "Cannot create workload payload");
    std::mt19937_64 rng(0x435953594e434b31ull);
    std::vector<char> block(mib);
    for (uintmax_t at = 0; at < bytes;) {
        auto count = size_t(std::min(uintmax_t(block.size()), bytes - at));
        for (size_t i = 0; i < count;) {
            auto word = rng();
            for (size_t j = 0; j < 8 && i < count; ++j, ++i)
                block[i] = static_cast<char>((word >> (j * 8)) & 255);
        }
        output.write(block.data(), static_cast<std::streamsize>(count));
        check(bool(output), "Cannot stream generated workload bytes");
        at += count;
    }
    output.close();
    check(bool(output), "Cannot flush workload payload");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Pass an existing absolute work parent; this opt-in test needs 18 GiB free\n";
        return 2;
    }
    fs::path work;
    Json report = {
        {"success", false},
        {"payload_bytes", bytes},
        {"transport", "production HTTP on loopback; separate real-WireGuard test"},
        {"generator", "mt19937_64 seed 0x435953594e434b31, little-endian words, 1 MiB buffer"}};
    auto start = std::chrono::steady_clock::now();
    try {
        auto parent = fs::u8path(argv[1]);
        check(parent.is_absolute() && fs::is_directory(parent) && !path_is_link(parent),
              "Use an existing absolute unlinked work parent");
        work = fs::canonical(parent) / ("large-workload-" + random_id());
        check(fs::create_directory(work), "Cannot reserve workload directory");
        auto data = work / "data";
        fs::create_directory(data);
        check(fs::space(data).available > 8 * bytes,
              "Large workload requires at least 18 GiB free");
        auto mark = [&](const char *name) {
            auto now = std::chrono::steady_clock::now();
            report["elapsed_seconds"][name] = std::chrono::duration<double>(now - start).count();
            atomic_write(work / "report.json", report.dump(2), true);
            std::cout << name << '\n' << std::flush;
        };
        Core sender(data / "sender-state"), receiver(data / "receiver-state");
        auto source = data / "source", remote_root = data / "receiver-project";
        project(sender, source);
        project(receiver, remote_root);
        auto payload = source / fs::u8path("asset été.bin");
        generate(payload);
        auto original_hash = sha256_file(payload);
        report["original_sha256"] = original_hash;
        mark("generated-2304-MiB-plus-123-bytes");
#ifdef _WIN32
        _putenv_s("CYSYNCK_MESH_TOKEN", (random_id() + random_id()).c_str());
#else
        setenv("CYSYNCK_MESH_TOKEN", (random_id() + random_id()).c_str(), 1);
#endif
        network_fixture(receiver, peer_id(sender), 8485);
        Receiver listener;
        auto port = listener.start(receiver);
        network_fixture(sender, peer_id(receiver), port);
        std::atomic<bool> interrupted = false;
        std::exception_ptr monitor_error;
        std::jthread interrupter([&](std::stop_token stop) {
            try {
                while (!stop.stop_requested()) {
                    auto transfers =
                        receiver.request("POST", "/api/network/transfers", {{"project", "large"}});
                    for (const auto &file : transfers.at("files"))
                        if (file.at("direction") == "in" &&
                            file.at("acknowledged_bytes").get<uintmax_t>() >= 32 * mib) {
                            listener.stop();
                            interrupted = true;
                            return;
                        }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            } catch (...) {
                monitor_error = std::current_exception();
            }
        });
        auto partial = cli(sender, {"send-file", "large", peer_id(receiver), "asset été.bin"});
        interrupter.request_stop();
        interrupter.join();
        listener.stop();
        if (monitor_error)
            std::rethrow_exception(monitor_error);
        check(interrupted && !partial.at("delivered").get<bool>() &&
                  partial.at("acknowledged_bytes").get<uintmax_t>() >= 32 * mib &&
                  partial.at("acknowledged_bytes").get<uintmax_t>() < bytes,
              "Large transfer was not actually interrupted after acknowledged payload bytes");
        report["interrupted_at_bytes"] = partial.at("acknowledged_bytes");
        mark("interrupted-after-acknowledged-data");
        fs::rename(payload, data / "original.bin");
        atomic_write(payload, "Changed while the receiver was offline\n");
        Core restarted_sender(sender.state()), restarted_receiver(receiver.state());
        check(listener.start(restarted_receiver, port) == port,
              "Receiver did not restart on its original port");
        auto resumed = cli(restarted_sender, {"transfers", "resume", "large", partial.at("id")});
        listener.stop();
        check(resumed.at("delivered") == true &&
                  resumed.at("acknowledged_bytes").get<uintmax_t>() == bytes,
              "Multi-GiB resume did not confirm all original bytes");
        auto received = cli(restarted_receiver, {"received", "verify", "large", partial.at("id")});
        auto received_file = restarted_receiver.state() / "inbox/large" /
                             (partial.at("id").get<std::string>() + ".received");
        check(received.at("ok") == true && fs::file_size(received_file) == bytes &&
                  sha256_file(received_file) == original_hash,
              "Resumed multi-GiB payload differs from the frozen original");
        check(read_file(payload) == "Changed while the receiver was offline\n",
              "Resume overwrote the edited source");
        report["resumed_bytes"] = resumed.at("acknowledged_bytes");
        mark("resumed-after-core-restart-and-source-edit");
        fs::remove(payload);
        fs::rename(data / "original.bin", payload);
        auto first = cli(restarted_sender, {"backup", "large"});
        check(first.at("block_storage").at("new_block_bytes").get<uintmax_t>() == bytes,
              "Initial nonrepeating CDC fixture unexpectedly reused block bytes");
        check(restarted_sender
                      .request("POST", "/api/projects/large/verify", {{"snapshot", first.at("id")}})
                      .at("ok") == true,
              "Original large CDC snapshot did not verify");
        mark("captured-and-verified-large-cdc-snapshot");
        {
            std::ifstream input(payload, std::ios::binary);
            std::ofstream output(data / "shifted.bin", std::ios::binary | std::ios::trunc);
            auto prefix = std::string(4097, 'P');
            output.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
            std::vector<char> buffer(mib);
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                output.write(buffer.data(), input.gcount());
            }
            check(input.eof() && bool(output), "Cannot stream shifted CDC source");
        }
        fs::rename(payload, data / "original.bin");
        fs::rename(data / "shifted.bin", payload);
        auto shifted_hash = sha256_file(payload);
        auto shifted = cli(restarted_sender, {"backup", "large"});
        auto reused = shifted.at("block_storage").at("reused_block_bytes").get<uintmax_t>();
        check(
            reused >= bytes * 95 / 100,
            "Prefix insertion did not recover CDC boundaries for at least 95% of real large data");
        report["cdc"] = {{"initial_new_bytes", first.at("block_storage").at("new_block_bytes")},
                         {"shifted_new_bytes", shifted.at("block_storage").at("new_block_bytes")},
                         {"reused_bytes", reused},
                         {"reused_fraction", double(reused) / double(bytes + 4097)},
                         {"shifted_sha256", shifted_hash}};
        mark("shifted-large-cdc-snapshot-reused-content");
        auto destination = data / "restored";
        restarted_sender.request(
            "POST", "/api/projects/large/restore",
            {{"snapshot", shifted.at("id")}, {"destination", path_string(destination)}});
        check(fs::file_size(destination / payload.filename()) == bytes + 4097 &&
                  sha256_file(destination / payload.filename()) == shifted_hash,
              "Large CDC restore differs from the shifted source bytes");
        report["peak_resident_bytes"] = peak_memory();
        check(report.at("peak_resident_bytes").get<uintmax_t>() < 512 * mib,
              "Large streamed workload exceeded 512 MiB measured peak resident memory");
        mark("restored-and-hashed-all-shifted-bytes");
        report["success"] = true;
        // data is a newly reserved child of this run; retain only the small report.
        check(data.parent_path() == work && !path_is_link(data), "Refuse foreign workload cleanup");
        fs::remove_all(data);
    } catch (const std::exception &error) {
        report["error"] = error.what();
    }
    if (!work.empty())
        atomic_write(work / "report.json", report.dump(2), true);
    std::cout << report.dump(2) << "\nReport: " << path_string(work / "report.json") << '\n';
    return report.at("success").get<bool>() ? 0 : 1;
}
