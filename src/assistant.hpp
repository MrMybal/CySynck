#pragma once
#include <cysynck/core.hpp>
#include <cymcp/ai.hpp>
#include <cymcp/http.hpp>
#include <cymcp/mcp.hpp>
#include <cymcp/performance.hpp>
#include <cymcp/changes.hpp>
#include <deque>
#include <future>
#include <optional>

namespace cysynck::assistant {
// Workers submit application UI actions; only the application's UI thread drains
// them. Closing the bridge rejects queued work before joining provider workers.
class DesktopBridge {
    struct Request;
    std::mutex mutex_;
    std::deque<std::shared_ptr<Request>> queue_;
    bool closed_ = false;

  public:
    using Handler = std::function<Json(const std::string &, const Json &)>;
    Json invoke(const std::string &, const Json &, const cymcp::CallContext &);
    void drain(const Handler &);
    void close();
};
struct Options {
    cymcp::LocalAccessProfile access_profile = cymcp::LocalAccessProfile::free;
    bool compact = true, notifications = true, disk_spool = false, metrics = false;
    std::size_t inline_bytes = 16384, spool_quota_mib = 128, context_bytes = 0;
    int cache_ttl_ms = 30000, retention_seconds = 300;
    static Options load(const fs::path &);
    static void save_access_profile(const fs::path &, cymcp::LocalAccessProfile);
};
const char *access_profile_name(cymcp::LocalAccessProfile);
cymcp::LocalAccessProfile parse_access_profile(const std::string &);
// Core serializes its business requests and locks the on-disk profile. No handler
// manipulates ImGui, Unreal objects or the process-wide graphical state.
class Host {
    Core &core_;
    Options options_;
    std::atomic_bool persistent_{false};
    std::shared_ptr<DesktopBridge> desktop_;
    std::mutex receiver_mutex_;
    std::unique_ptr<HttpServer> receiver_;
    std::thread receiver_thread_;
    std::string receiver_address_;
    int receiver_port_ = 0;
    std::atomic_bool writes_{false};
    std::shared_ptr<cymcp::Registry> registry_;
    std::shared_ptr<cymcp::ResourceRegistry> resources_;
    std::shared_ptr<cymcp::ChangeFeed> changes_;
    std::shared_ptr<cymcp::PerformanceMonitor> performance_;
    std::unique_ptr<cymcp::ToolGateway> gateway_;
    std::unique_ptr<cymcp::McpServer> server_;
    std::unique_ptr<cymcp::LocalHttpServer> http_;
    std::jthread activity_watcher_;
    std::mutex remotes_mutex_;
    std::mutex observation_mutex_;
    std::string activity_digest_;
    std::vector<std::shared_ptr<cymcp::McpClient>> remotes_;
    void register_operations();
    void register_extended_operations();
    static Json sanitize_result(Json);
    Json receiver_action(const std::string &);
    void watch_activity();
    void add(std::string name, std::string description, std::string method, std::string route,
             bool read_only, Json properties = Json::object(), Json required = Json::array(),
             bool project = false);

  public:
    explicit Host(Core &);
    explicit Host(Core &, Options, bool persistent = false,
                  std::shared_ptr<DesktopBridge> desktop = {});
    ~Host();
    void allow_writes(bool value) { writes_.store(value); }
    bool writes_allowed() const { return !access().control_operations || writes_.load(); }
    cymcp::LocalAccessSettings access() const {
        return cymcp::local_access_settings(options_.access_profile);
    }
    std::shared_ptr<const cymcp::Registry> registry() const { return registry_; }
    std::shared_ptr<const cymcp::Registry> tools(bool full = false) const;
    std::shared_ptr<cymcp::ResourceRegistry> resources() const { return resources_; }
    const Options &options() const { return options_; }
    std::shared_ptr<cymcp::PerformanceMonitor> performance() const { return performance_; }
    Json info() const;
    Json command(const std::vector<std::string> &);
    void stdio(bool full);
    int listen(int port = 0, bool full = false);
    void stop();
    void changed();
    void observe(const Json &events);
    void connect_remote(const std::string &url, const std::string &token,
                        const std::vector<std::string> &allowed_tools,
                        const cymcp::Cancellation &cancellation = {});
};
// Returns false for an ordinary legacy CLI command.
bool handles(const std::vector<std::string> &);
int run(Core &, const std::vector<std::string> &);
} // namespace cysynck::assistant
