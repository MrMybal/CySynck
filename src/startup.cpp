#include "startup.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <regex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace cysynck::desktop {
fs::path current_executable() {
#ifdef _WIN32
    std::wstring buffer(32768, 0);
    auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length == buffer.size())
        throw std::runtime_error("Cannot locate the executable");
    buffer.resize(length);
    return fs::canonical(buffer);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, 0);
    if (_NSGetExecutablePath(buffer.data(), &size))
        throw std::runtime_error("Cannot locate the executable");
    return fs::canonical(fs::u8path(buffer.c_str()));
#else
    return fs::canonical("/proc/self/exe");
#endif
}
fs::path desktop_executable() {
    return current_executable().parent_path() /
#ifdef _WIN32
           "cysynck-desktop.exe";
#else
           "cysynck-desktop";
#endif
}
StartupBackend native_startup_backend() {
#ifdef _WIN32
    return StartupBackend::windows;
#elif defined(__APPLE__)
    return StartupBackend::macos;
#else
    return StartupBackend::linux;
#endif
}
fs::path user_startup_directory() {
#ifdef _WIN32
    PWSTR path = nullptr;
    auto result = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DONT_VERIFY, nullptr, &path);
    fs::path directory = path ? fs::path(path) : fs::path{};
    CoTaskMemFree(path);
    if (FAILED(result))
        throw std::runtime_error("Cannot locate the user's Startup folder");
    return directory;
#else
    auto home = fs::u8path(env("HOME"));
    if (!home.is_absolute())
        throw std::runtime_error("Cannot locate the user's home directory");
#ifdef __APPLE__
    return home / "Library/LaunchAgents";
#else
    auto config = fs::u8path(env("XDG_CONFIG_HOME"));
    return (config.is_absolute() ? config : home / ".config") / "autostart";
#endif
#endif
}
static void valid_text(const std::string &text) {
    for (unsigned char ch : text)
        if (ch < 32 || ch == 127)
            throw std::runtime_error("Login launch paths cannot contain control characters");
}
static fs::path normalized(const fs::path &path) {
    if (path.empty())
        throw std::runtime_error("Login launch paths cannot be empty");
    auto result = fs::weakly_canonical(fs::absolute(path));
    valid_text(path_string(result));
    return result;
}
LoginStartup::LoginStartup(LaunchSpec spec, fs::path directory, StartupBackend backend)
    : spec_(std::move(spec)), directory_(normalized(directory)), backend_(backend) {
    spec_.executable = normalized(spec_.executable);
    spec_.state = normalized(spec_.state);
    for (auto &plugin : spec_.plugins)
        plugin = normalized(plugin);
    auto identity_path = path_string(spec_.state);
#ifdef _WIN32
    auto native = spec_.state.native();
    auto size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, native.data(),
                              static_cast<int>(native.size()), nullptr, 0, nullptr, nullptr, 0);
    if (size > 0) {
        std::wstring lower(size, 0);
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, native.data(),
                      static_cast<int>(native.size()), lower.data(), size, nullptr, nullptr, 0);
        identity_path = path_string(fs::path(lower));
    }
#endif
    identity_ = sha256(identity_path);
    marker_ = "CySynck login / " + identity_;
}
fs::path LoginStartup::entry() const {
    auto id = identity_.substr(0, 24);
    if (backend_ == StartupBackend::windows)
        return directory_ / ("CySynck-" + id + ".lnk");
    if (backend_ == StartupBackend::macos)
        return directory_ / ("com.cysynck.desktop." + id + ".plist");
    return directory_ / ("cysynck-" + id + ".desktop");
}
std::vector<std::string> LoginStartup::arguments() const {
    std::vector<std::string> args{path_string(spec_.executable), "--state",
                                  path_string(spec_.state), "--background"};
    for (const auto &plugin : spec_.plugins) {
        args.push_back("--plugin");
        args.push_back(path_string(plugin));
    }
    return args;
}
static std::string xml(const std::string &value) {
    std::string out;
    for (char ch : value)
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += ch;
        }
    return out;
}
static std::string desktop_quote(const std::string &value) {
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '%')
            quoted += "%%";
        else {
            if (ch == '\\' || ch == '$' || ch == '`' || ch == '"')
                quoted += '\\';
            quoted += ch;
        }
    }
    quoted += '"';
    // Desktop-entry string escaping is applied before Exec argument quoting.
    std::string encoded;
    for (char ch : quoted) {
        if (ch == '\\')
            encoded += '\\';
        encoded += ch;
    }
    return encoded;
}
std::string LoginStartup::contents() const {
    auto args = arguments();
    if (backend_ == StartupBackend::linux) {
        if (args[0].find('=') != std::string::npos)
            throw std::runtime_error("Desktop entry executable paths cannot contain '='");
        std::string command;
        for (const auto &arg : args) {
            if (!command.empty())
                command += ' ';
            command += desktop_quote(arg);
        }
        return "# " + marker_ +
               "\n[Desktop Entry]\nType=Application\nVersion=1.0\nName=CySynck\nComment=Project "
               "synchronization\nExec=" +
               command +
               "\nTerminal=false\nStartupNotify=false\nHidden=false\nX-GNOME-Autostart-enabled="
               "true\n";
    }
    if (backend_ == StartupBackend::macos) {
        std::string body =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- " + marker_ +
            " -->\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n<key>Label</key><string>com.cysynck.desktop." +
            identity_.substr(0, 24) + "</string>\n<key>ProgramArguments</key><array>\n";
        for (const auto &arg : args)
            body += "<string>" + xml(arg) + "</string>\n";
        return body +
               "</array>\n<key>RunAtLoad</key><true/>\n<key>KeepAlive</key><false/"
               ">\n<key>LimitLoadToSessionType</key><string>Aqua</string>\n<key>ProcessType</"
               "key><string>Interactive</string>\n</dict></plist>\n";
    }
    throw std::runtime_error("Windows login entries use native shortcuts");
}
#ifdef _WIN32
static std::wstring wide(const std::string &value) { return fs::u8path(value).native(); }
static std::string utf8(const wchar_t *value) {
    auto size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), size, nullptr, nullptr);
    if (!out.empty())
        out.pop_back();
    return out;
}
static std::string windows_quote(const std::string &value) {
    std::string out = "\"";
    size_t slashes = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++slashes;
            continue;
        }
        out.append(ch == '"' ? slashes * 2 + 1 : slashes, '\\');
        slashes = 0;
        out += ch;
    }
    out.append(slashes * 2, '\\');
    out += '"';
    return out;
}
static std::wstring windows_arguments(const std::vector<std::string> &args) {
    std::string command;
    for (size_t i = 1; i < args.size(); ++i) {
        if (!command.empty())
            command += ' ';
        command += windows_quote(args[i]);
    }
    if (wide(command).size() > 30000)
        throw std::runtime_error("Login launch arguments are too long");
    return wide(command);
}
struct ComApartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComApartment() {
        if (FAILED(result) && result != RPC_E_CHANGED_MODE)
            throw std::runtime_error("Cannot initialize Windows shortcuts");
    }
    ~ComApartment() {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
};
template <class T> struct ComPtr {
    T *p = nullptr;
    ~ComPtr() {
        if (p)
            p->Release();
    }
    T *operator->() const { return p; }
};
static void require(HRESULT value) {
    if (FAILED(value))
        throw std::runtime_error("Cannot read or write the Windows login shortcut");
}
struct ShortcutInfo {
    std::string marker;
    fs::path executable;
    std::wstring arguments;
};
static ShortcutInfo shortcut(const fs::path &path) {
    ComApartment apartment;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    require(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                             reinterpret_cast<void **>(&link.p)));
    require(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file.p)));
    require(file->Load(path.c_str(), STGM_READ));
    std::wstring description(1024, 0), target(32768, 0), args(32768, 0);
    require(link->GetDescription(description.data(), static_cast<int>(description.size())));
    require(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH));
    require(link->GetArguments(args.data(), static_cast<int>(args.size())));
    return {utf8(description.c_str()), fs::path(target.c_str()), std::wstring(args.c_str())};
}
static void save_shortcut(const fs::path &path, const LaunchSpec &spec,
                          const std::vector<std::string> &args, const std::string &marker) {
    ComApartment apartment;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    require(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                             reinterpret_cast<void **>(&link.p)));
    require(link->SetPath(spec.executable.c_str()));
    require(link->SetArguments(windows_arguments(args).c_str()));
    require(link->SetWorkingDirectory(spec.executable.parent_path().c_str()));
    require(link->SetDescription(wide(marker).c_str()));
    require(link->SetShowCmd(SW_SHOWNORMAL));
    require(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file.p)));
    require(file->Save(path.c_str(), TRUE));
}
#endif
bool LoginStartup::owned() const {
    auto path = entry();
    if (path_is_link(path) || !fs::is_regular_file(path))
        return false;
#ifdef _WIN32
    if (backend_ == StartupBackend::windows)
        return shortcut(path).marker == marker_;
#endif
    auto text = read_file(path, 128 * 1024);
    if (backend_ == StartupBackend::linux)
        return text.starts_with("# " + marker_ + "\n");
    return text.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!-- " + marker_ +
                            " -->\n");
}
bool LoginStartup::matches() const {
#ifdef _WIN32
    if (backend_ == StartupBackend::windows) {
        auto info = shortcut(entry());
        return info.executable.lexically_normal() == spec_.executable.lexically_normal() &&
               info.arguments == windows_arguments(arguments());
    }
#endif
    return read_file(entry(), 128 * 1024) == contents();
}
Json LoginStartup::status() const {
    Json result{{"entry", path_string(entry())},
                {"registered", false},
                {"owned", false},
                {"up_to_date", false},
                {"backend", backend_ == StartupBackend::windows ? "windows"
                            : backend_ == StartupBackend::macos ? "macos"
                                                                : "linux"},
                {"arguments", arguments()}};
    try {
        result["registered"] = fs::exists(entry()) || path_is_link(entry());
        if (result["registered"].get<bool>()) {
            result["owned"] = owned();
            if (result["owned"].get<bool>())
                result["up_to_date"] = matches();
        }
    } catch (const std::exception &error) {
        result["error"] = error.what();
    }
    return result;
}
Json LoginStartup::enable() const {
    DiskLock lock(spec_.state, "startup.lock");
    if (!fs::is_regular_file(spec_.executable))
        throw std::runtime_error(
            "The desktop executable must be installed before registering at login");
    for (const auto &plugin : spec_.plugins)
        if (!fs::is_regular_file(plugin))
            throw std::runtime_error("A configured plugin is missing");
    auto previous = status();
    if (previous.at("registered").get<bool>() && !previous.at("owned").get<bool>())
        throw std::runtime_error("An unrelated login entry already occupies this path");
    fs::create_directories(directory_);
#ifdef _WIN32
    if (backend_ == StartupBackend::windows) {
        auto pending = directory_ / ("cysynck-" + random_id() + ".pending");
        try {
            save_shortcut(pending, spec_, arguments(), marker_);
            if (!MoveFileExW(pending.c_str(), entry().c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot publish the Windows login shortcut");
        } catch (...) {
            std::error_code ignored;
            fs::remove(pending, ignored);
            throw;
        }
    } else
#endif
        atomic_write(entry(), contents());
    auto result = status();
    if (!result.at("up_to_date").get<bool>())
        throw std::runtime_error("The registered login entry could not be verified");
    return result;
}
Json LoginStartup::disable() const {
    DiskLock lock(spec_.state, "startup.lock");
    if (!fs::exists(entry()) && !path_is_link(entry()))
        return status();
    if (!owned())
        throw std::runtime_error("Refusing to remove an unrelated login entry");
    fs::remove(entry());
    return status();
}
Json startup_command(LoginStartup &startup, const std::vector<std::string> &args) {
    if (args.size() != 3 || args[0] != "desktop" || args[1] != "startup")
        throw std::runtime_error("Use desktop startup status|enable|disable");
    if (args[2] == "status")
        return startup.status();
    if (args[2] == "enable")
        return startup.enable();
    if (args[2] == "disable")
        return startup.disable();
    if (args[2] == "cleanup")
        return remove_installation_startup(startup.spec().executable,
                                           startup.entry().parent_path());
    throw std::runtime_error("Unknown desktop startup command");
}
Json remove_installation_startup(const fs::path &executable, const fs::path &directory) {
    Json removed = Json::array(), skipped = Json::array();
#ifdef _WIN32
    if (!fs::exists(directory))
        return {{"removed", removed}, {"skipped", skipped}};
    auto expected = normalized(executable);
    for (const auto &file : fs::directory_iterator(directory)) {
        if (path_is_link(file.path()) || !file.is_regular_file() ||
            !std::regex_match(path_string(file.path().filename()),
                              std::regex("CySynck-[a-f0-9]{24}\\.lnk")))
            continue;
        try {
            auto info = shortcut(file.path());
            if (info.executable.lexically_normal() != expected.lexically_normal())
                continue;
            int count = 0;
            auto line = L"cysynck-placeholder " + info.arguments;
            auto values = CommandLineToArgvW(line.c_str(), &count);
            if (!values)
                throw std::runtime_error("Cannot inspect startup arguments");
            std::vector<std::string> args;
            for (int i = 1; i < count; ++i)
                args.push_back(utf8(values[i]));
            LocalFree(values);
            fs::path state;
            for (size_t i = 0; i + 1 < args.size(); ++i)
                if (args[i] == "--state") {
                    state = fs::u8path(args[i + 1]);
                    break;
                }
            if (!state.is_absolute())
                continue;
            LoginStartup candidate({expected, state, {}}, directory, StartupBackend::windows);
            if (candidate.entry() != file.path() || !candidate.owned())
                continue;
            std::unique_ptr<DiskLock> lock;
            if (fs::is_directory(state))
                lock = std::make_unique<DiskLock>(state, "startup.lock");
            // Recheck after taking the profile lock; a concurrent update may move the app.
            if (shortcut(file.path()).executable.lexically_normal() ==
                    expected.lexically_normal() &&
                candidate.owned()) {
                fs::remove(file.path());
                removed.push_back(path_string(file.path()));
            }
        } catch (const std::exception &error) {
            skipped.push_back({{"entry", path_string(file.path())}, {"error", error.what()}});
        }
    }
#else
    throw std::runtime_error(
        "Installation cleanup is currently available for Windows shortcuts only");
#endif
    return {{"removed", removed}, {"skipped", skipped}};
}
} // namespace cysynck::desktop
