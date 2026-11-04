#include <cysynck/core.hpp>
#include "process_stream.hpp"
#include <picosha2.h>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
using namespace cysynck;
static int checks = 0;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
    ++checks;
}
template <class F> static void rejects(F call, const char *expected) {
    try {
        call();
    } catch (const std::exception &e) {
        if (std::string(e.what()).find(expected) == std::string::npos)
            throw;
        ++checks;
        return;
    }
    throw std::runtime_error("Expected process failure");
}
static std::array<char, 8192> pattern() {
    std::array<char, 8192> block{};
    for (size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<char>(i * 37);
    return block;
}
static int fixture(int argc, char **argv) {
    std::string mode = argv[1];
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    auto block = pattern();
    if (mode == "--emit" || mode == "--flood") {
        if (mode == "--flood")
            for (int i = 0; i < 1152; ++i)
                std::cerr.write(block.data(), block.size());
        uint64_t count = std::stoull(argv[2]);
        while (count && std::cout) {
            auto n = static_cast<size_t>(std::min<uint64_t>(count, block.size()));
            std::cout.write(block.data(), n);
            count -= n;
        }
        return std::cout ? 0 : 1;
    }
    if (mode == "--echo") {
        while (std::cin) {
            std::cin.read(block.data(), block.size());
            std::cout.write(block.data(), std::cin.gcount());
        }
        return 0;
    }
    if (mode == "--sleep") {
        std::cout << "started" << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(4));
        return 0;
    }
    if (mode == "--exit") {
        std::cout << "done";
        std::cerr << "failure";
        return 7;
    }
    if (mode == "--cwd") {
        std::cout << path_string(fs::current_path());
        return 0;
    }
    if (mode == "--descendant") {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::ofstream(fs::u8path(argv[2])) << "leaked";
        return 0;
    }
    if (mode == "--spawn") {
#ifdef _WIN32
        auto command = L"\"" + fs::u8path(argv[0]).wstring() + L"\" --descendant \"" +
                       fs::u8path(argv[2]).wstring() + L"\"";
        STARTUPINFOW start{};
        start.cb = sizeof(start);
        start.dwFlags = STARTF_USESTDHANDLES;
        start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        start.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &start, &process))
            return 9;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        auto pid = fork();
        if (pid < 0)
            return 9;
        if (!pid) {
            execl(argv[0], argv[0], "--descendant", argv[2], nullptr);
            _exit(127);
        }
#endif
        std::cout << "spawned" << std::flush;
        return 0;
    }
    return 8;
}
int main(int argc, char **argv) {
    if (argc > 1)
        return fixture(argc, argv);
    auto run = fs::current_path() / "process-stream-test-data" / random_id();
    fs::create_directories(run);
    auto executable = path_string(fs::absolute(fs::u8path(argv[0])));
    try {
        constexpr uint64_t length = 17 * 1024 * 1024 + 23;
        picosha2::hash256_one_by_one actual, expected;
        uint64_t bytes = 0;
        size_t largest = 0;
        auto result = process_stream({executable, "--emit", std::to_string(length)},
                                     [&](const char *data, size_t n) {
                                         actual.process(data, data + n);
                                         bytes += n;
                                         largest = std::max(largest, n);
                                     });
        auto block = pattern();
        for (uint64_t left = length; left;) {
            auto n = static_cast<size_t>(std::min<uint64_t>(left, block.size()));
            expected.process(block.data(), block.data() + n);
            left -= n;
        }
        actual.finish();
        expected.finish();
        check(result.code == 0 && result.out.empty() && bytes == length,
              "Streaming stdout is complete beyond the capture limit and is not accumulated");
        check(picosha2::get_hash_hex_string(actual) == picosha2::get_hash_hex_string(expected),
              "Binary stdout retains all byte values and the final partial block");
        check(largest <= 16384, "The sink receives bounded pipe chunks");
        auto captured = process({executable, "--emit", std::to_string(9 * 1024 * 1024)});
        check(captured.code == 0 && captured.out.size() == 8 * 1024 * 1024,
              "The original capture API retains its bounded behavior");
        bytes = 0;
        auto flood = process_stream({executable, "--flood", "1048577"},
                                    [&](const char *, size_t n) { bytes += n; });
        check(flood.code == 0 && bytes == 1048577 && flood.error.size() == 8 * 1024 * 1024,
              "Stderr is drained concurrently and remains bounded");
        std::string input(2 * 1024 * 1024 + 17, 'q'), echo;
        process_stream(
            {executable, "--echo"}, [&](const char *p, size_t n) { echo.append(p, n); }, {}, input);
        check(echo == input, "Simultaneous stdin and stdout avoid pipe deadlocks");
        std::string emitted;
        auto nonzero = process_stream({executable, "--exit"},
                                      [&](const char *p, size_t n) { emitted.append(p, n); });
        check(nonzero.code == 7 && emitted == "done" && nonzero.error == "failure",
              "Nonzero exit status and separate diagnostics are preserved");
        auto started = std::chrono::steady_clock::now();
        bytes = 0;
        rejects(
            [&] {
                process_stream(
                    {executable, "--emit", "100000000000"},
                    [&](const char *, size_t n) {
                        bytes += n;
                        if (bytes >= 131072)
                            throw std::runtime_error("fixture sink failure");
                    },
                    {}, {}, 30);
            },
            "fixture sink failure");
        check(std::chrono::steady_clock::now() - started < std::chrono::seconds(8) &&
                  bytes < 262144,
              "A failing sink cancels its child promptly without draining unbounded output");
        emitted.clear();
        started = std::chrono::steady_clock::now();
        rejects(
            [&] {
                process_stream(
                    {executable, "--sleep"}, [&](const char *p, size_t n) { emitted.append(p, n); },
                    {}, {}, 1);
            },
            "timed out");
        check(emitted == "started" &&
                  std::chrono::steady_clock::now() - started < std::chrono::seconds(6),
              "Timeout preserves delivered bytes and closes all pipes");
        auto marker = run / "descendant-marker";
        emitted.clear();
        auto descendant = process_stream(
            {executable, "--spawn", path_string(marker)},
            [&](const char *p, size_t n) { emitted.append(p, n); }, {}, {}, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        check(descendant.code == 0 && emitted == "spawned" && !fs::exists(marker),
              "Owned descendants cannot hold stream pipes open or survive the leader");
        auto directory = run / fs::u8path("directory été");
        fs::create_directory(directory);
        emitted.clear();
        process_stream(
            {executable, "--cwd"}, [&](const char *p, size_t n) { emitted.append(p, n); },
            directory);
        check(fs::equivalent(fs::u8path(emitted), directory),
              "Streamed processes support Unicode working directories");
        rejects([&] { process_stream({}, [](const char *, size_t) {}); }, "No process command");
        rejects([&] { process_stream({executable}, {}); }, "sink is required");
        rejects(
            [&] {
                process_stream({executable, "--sleep"}, [](const char *, size_t) {}, {}, {}, 0);
            },
            "timeout");
        for (int i = 0; i < 12; ++i) {
            try {
                process_stream({executable, "--emit", "64"},
                               [](const char *, size_t) { throw std::runtime_error("late sink"); });
                throw std::runtime_error("Missing late failure");
            } catch (const std::exception &e) {
                if (std::string(e.what()) != "late sink")
                    throw;
            }
        }
        check(true, "Reader failures while the child exits remain observable across repeated runs");
        check(process({executable, "--exit"}).code == 7,
              "Capture processes still work after stream cancellation and timeout");
        std::cout << "PASS " << checks
                  << " process streaming checks; fixtures: " << path_string(run) << '\n';
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
