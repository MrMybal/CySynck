#pragma once
#include <cysynck/core.hpp>

namespace cysynck::desktop {
enum class StartupBackend { windows, freedesktop, macos };
struct LaunchSpec {
    fs::path executable, state;
    std::vector<fs::path> plugins;
};
fs::path current_executable();
fs::path desktop_executable();
fs::path desktop_executable(const fs::path &executable, StartupBackend backend);
StartupBackend native_startup_backend();
fs::path user_startup_directory();
// An explicit destination supports isolated tests and portable format generation.
// Normal UI/CLI callers always use user_startup_directory().
class LoginStartup {
    LaunchSpec spec_;
    fs::path directory_;
    StartupBackend backend_;
    std::string identity_, marker_;
    bool owned() const;
    bool matches() const;
    friend Json remove_installation_startup(const fs::path &, const fs::path &);

  public:
    LoginStartup(LaunchSpec spec, fs::path directory, StartupBackend backend);
    const LaunchSpec &spec() const { return spec_; }
    fs::path entry() const;
    std::vector<std::string> arguments() const;
    std::string contents() const;
    Json status() const;
    Json enable() const;
    Json disable() const;
};
Json startup_command(LoginStartup &startup, const std::vector<std::string> &args);
Json remove_installation_startup(const fs::path &executable, const fs::path &directory);
} // namespace cysynck::desktop
