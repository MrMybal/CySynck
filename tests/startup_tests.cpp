#include "startup.hpp"
#include <tinyxml2.h>
#include <iostream>
#include <functional>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
#ifdef CYSYNCK_TEST_GIO
#include <gio/gdesktopappinfo.h>
#endif
using namespace cysynck;
using namespace cysynck::desktop;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(std::function<void()> fn, const char *message) {
    try {
        fn();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
static void await_arguments(const fs::path &state, const std::vector<std::string> &expected) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto output = state / "startup-observed.json";
    std::string bytes;
    bool readable = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (fs::exists(output)) {
            try {
                bytes = read_file(output);
                readable = true;
                break;
            } catch (const std::runtime_error &) {
                // A Windows write-through rename may expose the path before releasing it.
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(readable, "The OS launcher did not publish readable startup arguments");
    auto observed = Json::parse(bytes);
    std::vector<std::string> actual = observed;
    check(actual == std::vector<std::string>(expected.begin() + 1, expected.end()),
          "The OS launcher must preserve every startup argument");
}
static int run(const std::vector<std::string> &args) {
    // This helper acts as the desktop only when launched by an isolated test entry.
    if (args.size() >= 2 && args[0] == "--state") {
        atomic_write(fs::u8path(args[1]) / "startup-observed.json", Json(args).dump(), true);
        return 0;
    }
    auto root = fs::current_path() / "startup-test-data" / random_id();
    try {
        Core core(root / fs::u8path("Profil & éspace % $données `"));
        auto executable = current_executable();
        auto bin = root / fs::u8path("Application éspace %") / "bin";
        fs::create_directories(bin);
        auto copy = bin / executable.filename();
        fs::copy_file(executable, copy);
#ifdef _WIN32
        fs::copy_file(executable.parent_path() / "cysynck_core.dll", bin / "cysynck_core.dll");
#endif
        auto plugin = root / fs::u8path("Extensions éspace %/example.plugin");
        atomic_write(plugin, "test fixture, never loaded");
        LaunchSpec spec{copy, core.state(), {plugin}};
        for (auto backend : {StartupBackend::freedesktop, StartupBackend::macos}) {
            auto entries =
                root / (backend == StartupBackend::freedesktop ? "xdg-autostart" : "LaunchAgents");
            LoginStartup entry(spec, entries, backend);
            check(!entry.status().at("registered").get<bool>(),
                  "A profile must not register itself by default");
            auto enabled = entry.enable();
            check(enabled.at("owned").get<bool>() && enabled.at("up_to_date").get<bool>(),
                  "An explicit login entry is created and verified");
            check(entry.arguments()[2] == path_string(core.state()) &&
                      entry.arguments().back() == path_string(plugin),
                  "The profile and explicit plugins are preserved");
            auto text = read_file(entry.entry());
            check(text.find("api-key") == std::string::npos &&
                      text.find("MESH_TOKEN") == std::string::npos,
                  "Login entries do not embed secrets");
            if (backend == StartupBackend::macos) {
                tinyxml2::XMLDocument doc;
                check(doc.Parse(text.c_str()) == tinyxml2::XML_SUCCESS,
                      "The launch agent must be valid XML");
                auto array =
                    doc.FirstChildElement("plist")->FirstChildElement("dict")->FirstChildElement(
                        "array");
                std::vector<std::string> args;
                for (auto value = array->FirstChildElement("string"); value;
                     value = value->NextSiblingElement("string"))
                    args.push_back(value->GetText());
                check(args == entry.arguments(), "XML parsing recovers the exact launch arguments");
            } else {
                check(text.find("%%") != std::string::npos &&
                          text.find("Terminal=false") != std::string::npos,
                      "Desktop entries escape literal percent signs and run without a terminal");
#ifdef CYSYNCK_TEST_GIO
                GError *error = nullptr;
                auto info =
                    g_desktop_app_info_new_from_filename(path_string(entry.entry()).c_str());
                check(info != nullptr, "GIO accepts the generated desktop entry");
                bool launched = g_app_info_launch(G_APP_INFO(info), nullptr, nullptr, &error);
                if (error) {
                    std::string message = error->message;
                    g_error_free(error);
                    g_object_unref(info);
                    throw std::runtime_error(message);
                }
                check(launched, "GIO launches the generated desktop entry");
                g_object_unref(info);
                await_arguments(core.state(), entry.arguments());
#endif
            }
            Core other(root / "other-profile");
            LoginStartup independent({copy, other.state(), {}}, entries, backend);
            independent.enable();
            check(independent.entry() != entry.entry(), "Profiles get separate login entries");
            atomic_write(entry.entry(), text + "\n");
            check(!entry.status().at("up_to_date").get<bool>(),
                  "Changed entries are reported as needing an update");
            entry.enable();
            check(entry.status().at("up_to_date").get<bool>(),
                  "An owned login entry can be repaired");
            entry.disable();
            check(!fs::exists(entry.entry()) && fs::exists(independent.entry()),
                  "Removing a profile leaves other profiles registered");
            entry.disable();
            atomic_write(entry.entry(), "Unrelated user entry\n");
            rejects([&] { entry.enable(); }, "Registration must not overwrite an unrelated entry");
            rejects([&] { entry.disable(); }, "Removal must not delete an unrelated entry");
            independent.disable();
        }
#ifdef _WIN32
        LoginStartup native(spec, root / "Windows-Startup", StartupBackend::windows);
        auto registered = native.enable();
        check(registered.at("up_to_date").get<bool>(),
              "Windows shortcut metadata is verified through COM");
        SHELLEXECUTEINFOW launch{};
        launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        auto path = native.entry();
        launch.lpFile = path.c_str();
        launch.lpVerb = L"open";
        launch.nShow = SW_HIDE;
        check(ShellExecuteExW(&launch), "Windows launches the isolated startup shortcut");
        await_arguments(core.state(), native.arguments());
        if (launch.hProcess) {
            WaitForSingleObject(launch.hProcess, 10000);
            CloseHandle(launch.hProcess);
        }
        native.disable();
        check(!fs::exists(native.entry()),
              "The native shortcut is removed without touching the profile");
        native.enable();
        Core second(root / "second-native-state"), foreign(root / "different-installation-state");
        LoginStartup second_entry({copy, second.state(), {}}, native.entry().parent_path(),
                                  StartupBackend::windows);
        LoginStartup foreign_entry({executable, foreign.state(), {}}, native.entry().parent_path(),
                                   StartupBackend::windows);
        second_entry.enable();
        foreign_entry.enable();
        auto cleaned = remove_installation_startup(copy, native.entry().parent_path());
        check(cleaned.at("removed").size() == 2 && fs::exists(foreign_entry.entry()),
              "Uninstall cleanup removes only profiles using this installation");
        check(fs::exists(second.state() / "registry.json"),
              "Installation cleanup preserves custom profiles");
        foreign_entry.disable();
#endif
        check(fs::exists(core.state() / "registry.json"),
              "Disabling startup preserves application state");
        rejects(
            [&] {
                LoginStartup invalid({copy, root / fs::u8path("bad\npath"), {}}, root / "entries",
                                     StartupBackend::freedesktop);
            },
            "Startup paths cannot inject new entry lines");
        std::cout << "PASS " << checks << " startup checks; fixtures: " << path_string(root)
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << path_string(root) << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        auto size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string value(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, value.data(), size, nullptr, nullptr);
        value.pop_back();
        args.push_back(value);
    }
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
