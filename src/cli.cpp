#include <atomic>
#include <csignal>
#include <cysynck/core.hpp>
#include <iostream>
#include <thread>
#include "startup.hpp"
#include "help.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
static volatile std::sig_atomic_t interrupted = 0;
static void interrupt(int) { interrupted = 1; }
static int run(std::vector<std::string> args) {
    try {
        auto state = cysynck::default_state();
        std::vector<std::string> plugins, commands;
        std::string server;
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--state" || args[i] == "--plugin" || args[i] == "--server") {
                auto flag = args[i];
                if (++i == args.size())
                    throw std::runtime_error("Missing option value");
                if (flag == "--state")
                    state = std::filesystem::u8path(args[i]);
                else if (flag == "--server")
                    server = args[i];
                else
                    plugins.push_back(args[i]);
            } else
                commands.push_back(args[i]);
        }
        if (!commands.empty() && (commands[0] == "docs" || commands[0] == "documentation")) {
            std::cout << cysynck::help::command(commands).dump(2) << '\n';
            return 0;
        }
        if (!server.empty()) {
            std::cout << cysynck::remote_cli(server, commands).dump(2) << '\n';
            return 0;
        }
        cysynck::Core core(state);
        if (!commands.empty() && commands[0] == "desktop") {
            cysynck::desktop::LaunchSpec launch{
                cysynck::desktop::desktop_executable(), core.state(), {}};
            for (const auto &plugin : plugins)
                launch.plugins.push_back(std::filesystem::u8path(plugin));
            cysynck::desktop::LoginStartup startup(std::move(launch),
                                                   cysynck::desktop::user_startup_directory(),
                                                   cysynck::desktop::native_startup_backend());
            std::cout << cysynck::desktop::startup_command(startup, commands).dump(2) << '\n';
            return 0;
        }
        for (auto &path : plugins)
            core.load_plugin(std::filesystem::u8path(path));
        if (commands.size() == 2 && commands[0] == "engine" &&
            (commands[1] == "run" || commands[1] == "start")) {
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            std::cout << core.request("POST", "/api/engine/start").dump(2)
                      << "\nOwned engine running; press Ctrl+C to stop.\n";
            std::cout.flush();
            int ticks = 0;
            while (!interrupted) {
                if (++ticks >= 15) {
                    core.tick();
                    ticks = 0;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            return 0;
        }
        if (!commands.empty() &&
            (commands[0] == "serve" ||
             (commands.size() > 1 && commands[0] == "chat" && commands[1] == "serve"))) {
            bool direct = commands[0] == "chat";
            auto host = std::string("127.0.0.1");
            int port = 8484;
            if (direct) {
                auto profile = core.request("GET", "/api/network/status").at("profile");
                if (profile.is_null())
                    throw std::runtime_error("Configure WireGuard first");
                host = profile.at("address");
                port = profile.at("mesh_port");
            }
            for (size_t i = 1; i + 1 < commands.size(); ++i) {
                if (commands[i] == "--host")
                    host = commands[++i];
                else if (commands[i] == "--port")
                    port = std::stoi(commands[++i]);
            }
            cysynck::HttpServer listener(core, direct);
            port = listener.bind(host, port);
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            std::jthread scheduler([&](std::stop_token stop) {
                int ticks = 15;
                while (!stop.stop_requested()) {
                    if (++ticks >= 15) {
                        if (!direct)
                            try {
                                core.tick();
                            } catch (const std::exception &e) {
                                std::cerr << e.what() << '\n';
                            }
                        ticks = 0;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            });
            std::jthread signalWatcher([&](std::stop_token stop) {
                while (!stop.stop_requested() && !interrupted)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (interrupted)
                    listener.stop();
            });
            std::cout << "CySynck " << (direct ? "direct receiver: " : "Web/API: http://") << host
                      << ':' << port << '\n';
            if (!direct)
                std::cout << "Token file: " << cysynck::path_string(state / "api-token") << '\n';
            std::cout.flush();
            listener.listen();
            scheduler.request_stop();
            signalWatcher.request_stop();
            return 0;
        }
        std::cout << cysynck::cli(core, commands).dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << cysynck::Json{{"error", e.what()}}.dump(
                         -1, ' ', false, cysynck::Json::error_handler_t::replace)
                  << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int count = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string value(count, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, value.data(), count, nullptr, nullptr);
        value.pop_back();
        args.push_back(value);
    }
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
