#include "desktop_support.hpp"
#include <functional>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#endif
using namespace cysynck;
using namespace cysynck::desktop;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void eventually(std::function<bool()> predicate, const char *message) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            if (predicate()) {
                ++checks;
                return;
            }
        } catch (const std::exception &) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error(message);
}
#ifdef _WIN32
static HWND find_window(uint64_t pid, const wchar_t *name) {
    struct Search {
        DWORD pid;
        const wchar_t *name;
        HWND found = nullptr;
    } search{static_cast<DWORD>(pid), name};
    EnumWindows(
        [](HWND window, LPARAM value) -> BOOL {
            auto &search = *reinterpret_cast<Search *>(value);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner != search.pid)
                return TRUE;
            wchar_t name[128]{};
            GetWindowTextW(window, name, 128);
            if (wcscmp(name, search.name) == 0) {
                search.found = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}
static int free_port() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        throw std::runtime_error("Cannot initialize test sockets");
    auto socket = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket == INVALID_SOCKET ||
        ::bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)))
        throw std::runtime_error("Cannot allocate a test port");
    int size = sizeof(address);
    getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size);
    int port = ntohs(address.sin_port);
    closesocket(socket);
    WSACleanup();
    return port;
}
static void native_background(const fs::path &root, const fs::path &desktop,
                              const fs::path &syncthing) {
    Core observer(root / "native-state");
    int gui = free_port(), sync = free_port();
    while (sync == gui)
        sync = free_port();
    observer.request("POST", "/api/engine/configure",
                     {{"executable", path_string(syncthing)},
                      {"port", gui},
                      {"listen_port", sync},
                      {"loopback_only", true},
                      {"auto_start", true}});
    fs::create_directories(root / "project");
    atomic_write(root / "project/file.txt", "background\n");
    observer.request("POST", "/api/projects",
                     {{"id", "background"},
                      {"name", "Background test"},
                      {"root", path_string(root / "project")},
                      {"mode", "backup"},
                      {"backup_interval_minutes", 1}});
    ChildProcess app(
        {path_string(desktop), "--state", path_string(observer.state()), "--background"}, root);
    HWND main = nullptr, tray = nullptr;
    eventually(
        [&] {
            main = find_window(app.pid(), L"CySynck");
            tray = find_window(app.pid(), L"CySynck notification area");
            return main && tray;
        },
        "Desktop and tray windows did not initialize");
    eventually(
        [&] { return observer.request("GET", "/api/engine/status").at("running").get<bool>(); },
        "Hidden desktop did not start its managed engine");
    check(!IsWindowVisible(main), "Background launch must keep the main window hidden");
    eventually(
        [&] { return !observer.request("GET", "/api/projects/background/snapshots").empty(); },
        "Scheduler did not back up a project while hidden");
    auto launch =
        process({path_string(desktop), "--state", path_string(observer.state())}, root, {}, 10);
    check(launch.code == 0, "Second launch must return to the existing desktop");
    eventually([&] { return IsWindowVisible(main); },
               "Second launch did not reopen the existing window");
    PostMessageW(main, WM_CLOSE, 0, 0);
    eventually([&] { return app.running() && !IsWindowVisible(main); },
               "Closing the window must preserve the background app");
    check(observer.request("GET", "/api/engine/status").at("running").get<bool>(),
          "Closing the window must preserve the engine");
    auto before = observer.request("GET", "/api/projects/background/snapshots").size();
    atomic_write(root / "project/file.txt", "changed while hidden\n");
    PostMessageW(tray, WM_COMMAND, static_cast<WPARAM>(TrayAction::backup_all), 0);
    eventually(
        [&] {
            return observer.request("GET", "/api/projects/background/snapshots").size() > before;
        },
        "Tray backup action did not finish while hidden");
    PostMessageW(tray, WM_COMMAND, static_cast<WPARAM>(TrayAction::quit), 0);
    check(app.wait(10000), "Quit must stop the desktop");
    check(!observer.request("GET", "/api/engine/status").at("running").get<bool>(),
          "Quit must stop the owned Syncthing child");
    observer.request("PUT", "/api/settings",
                     {{"ui_close_to_tray", false}, {"ui_start_hidden", true}});
    ChildProcess fallback({path_string(desktop), "--state", path_string(observer.state()),
                           "--background", "--no-tray"},
                          root);
    eventually(
        [&] {
            main = find_window(fallback.pid(), L"CySynck");
            return main && IsWindowVisible(main);
        },
        "A launch without a tray must stay accessible");
    PostMessageW(main, WM_CLOSE, 0, 0);
    check(fallback.wait(10000), "Closing a window without a tray must exit");
    check(!observer.request("GET", "/api/engine/status").at("running").get<bool>(),
          "Fallback exit must not leave an owned engine");
    std::cout << "PASS native background startup, scheduler, reopen, close, tray backup, quit and "
                 "no-tray fallback\n";
}
#endif
int main(int argc, char **argv) {
    auto root = fs::current_path() / "desktop-test-data" / random_id();
    try {
        Core core(root / "state");
        {
            Session primary(core.state()), secondary(core.state());
            check(primary.primary() && !secondary.primary(), "One desktop owns each profile");
            secondary.request_show();
            check(!secondary.take_show(), "Secondary instances cannot consume owner requests");
            check(primary.take_show() && !primary.take_show(), "Show requests are consumed once");
            atomic_write(core.state() / "desktop-requests" / (random_id() + ".json"), "not-json");
            atomic_write(core.state() / "desktop-requests" / (random_id() + ".json"),
                         "{\"action\":\"quit\"}");
            check(!primary.take_show(), "Invalid requests cannot trigger another action");
            check(fs::is_empty(core.state() / "desktop-requests"),
                  "Consumed requests do not accumulate");
        }
        Session recovered(core.state());
        check(recovered.primary(), "The desktop lease is released after exit");
        Core other(root / "other-state");
        Session independent(other.state());
        check(independent.primary(), "Different profiles remain independent");
        check(close_hides(true, true) && !close_hides(true, false) && !close_hides(false, true),
              "A window hides only when its preference and a tray permit it");
        auto settings = core.request("PUT", "/api/settings",
                                     {{"ui_close_to_tray", false}, {"ui_start_hidden", true}});
        check(!settings.at("ui_close_to_tray").get<bool>() &&
                  settings.at("ui_start_hidden").get<bool>(),
              "Desktop preferences survive through the settings API");
#ifdef _WIN32
        if (argc == 3)
            native_background(root, fs::canonical(fs::u8path(argv[1])),
                              fs::canonical(fs::u8path(argv[2])));
#endif
        std::cout << "PASS " << checks << " desktop checks; fixtures: " << path_string(root)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << path_string(root) << '\n';
        return 1;
    }
}
