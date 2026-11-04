#pragma once
#include <filesystem>
#include <memory>
#include <map>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace cysynck {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline constexpr const char *version = "0.19.0";
fs::path default_state();
std::string path_string(const fs::path &path);
bool path_is_link(const fs::path &path);
fs::path project_child(const fs::path &root, const std::string &relative);
std::string random_id();
std::string sha256(const std::string &bytes);
std::string sha256_file(const fs::path &path);
std::string timestamp();
std::string env(const char *name, const std::string &fallback = {});
std::string read_file(const fs::path &path, size_t limit = 1024 * 1024);
void atomic_write(const fs::path &path, const std::string &bytes, bool secret = false);
struct ProcessResult {
    int code;
    std::string out, error;
};
ProcessResult process(const std::vector<std::string> &arguments, const fs::path &cwd = {},
                      const std::string &input = {}, int timeout_seconds = 60);
std::string git(const Json &project, const std::vector<std::string> &arguments);

class DiskLock {
    void *handle_ = nullptr;
    int fd_ = -1;

  public:
    explicit DiskLock(const fs::path &state, const std::string &filename = "operations.lock",
                      bool wait = true);
    ~DiskLock();
    DiskLock(const DiskLock &) = delete;
    DiskLock &operator=(const DiskLock &) = delete;
};

class ChildProcess {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    ChildProcess(const std::vector<std::string> &arguments, const fs::path &cwd,
                 const std::map<std::string, std::string> &environment = {});
    ~ChildProcess();
    bool running();
    bool wait(int milliseconds);
    void terminate();
    uint64_t pid() const;
};

class Core {
    fs::path state_;
    std::mutex mutex_;
    Json registry_;
    struct Extension;
    std::vector<std::unique_ptr<Extension>> plugins_;
    std::unique_ptr<ChildProcess> engine_child_;
    std::unique_ptr<DiskLock> engine_lease_;
    bool engine_requested_ = false;
    bool engine_suppressed_ = false;
    std::string engine_error_;
    std::chrono::steady_clock::time_point engine_retry_{};
    Json engine_config() const;
    Json engine_action(const std::string &action, const Json &data);
    void maintain_engine();
    Json engine_request(const std::string &method, const std::string &path,
                        const Json &data = Json::object(), int timeout_seconds = 15);
    void load();
    void save();
    Json &project(const std::string &id);
    Json modes() const;
    Json backup(const Json &project, const std::string &reason);
    Json snapshots(const std::string &id) const;
    Json capture(const fs::path &path);
    Json changes(const Json &project, const Json &data);
    Json diff(const Json &project, const Json &data);
    Json compare(const Json &project, const Json &data);
    Json conflicts(const Json &project, const Json &data);
    Json resolution_plan(const Json &project, const Json &data);
    Json resolve_conflict(const Json &project, const Json &data);
    Json resolutions(const Json &project);
    Json export_resolution(const Json &project, const Json &data);
    Json acknowledge_resolution(const Json &project, const Json &data);
    Json restore(const Json &project, const Json &data);
    Json verify(const Json &project, const Json &data);
    Json restore_plan(const Json &project, const Json &data);
    Json storage(const Json &data);
    Json git_action(const Json &project, const std::string &action, const Json &data);
    Json sync_action(Json &project, const std::string &action, const Json &data);
    Json network_action(const std::string &action, const Json &data);
    Json direct_inbox(const Json &project, const std::string &action, const Json &data);
    Json transfer_receive(const Json &data, const std::string &bytes);
    Json transfer_send(const Json &data);
    Json transfer_action(const std::string &action, const Json &data);
    Json diagnostics(const Json &data);
    Json service(const std::string &action, const Json &data);
    void hook(const std::string &event, const Json &context);
    Json request_plugin_mode(const Json &project);
    void event(const std::string &project, const std::string &kind, const std::string &message);
    friend class HttpServer;

  public:
    explicit Core(fs::path state = default_state());
    ~Core();
    const fs::path &state() const { return state_; }
    Json request(const std::string &method, const std::string &path,
                 const Json &data = Json::object());
    void load_plugin(const fs::path &path);
    void tick();
    std::string api_token();
    Json direct_send(const Json &data, bool file);
    Json direct_receive(const Json &data, const std::string &remote_address,
                        const std::string &bytes = {});
};

class HttpServer {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    HttpServer(Core &core, bool direct = false);
    ~HttpServer();
    int bind(const std::string &host, int port);
    void listen();
    void stop();
};
Json cli(Core &core, const std::vector<std::string> &arguments);
Json remote_cli(const std::string &url, const std::vector<std::string> &arguments);
} // namespace cysynck
