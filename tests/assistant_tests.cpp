#include "assistant.hpp"
#include <algorithm>
#include <cymcp/chat_ui.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef CYSYNCK_ASSISTANT_CHAT_TESTS
#include <httplib.h>
#endif

using namespace cysynck;
using cysynck::assistant::Host;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
template <class Function> void rejects(Function work, const char *message) {
    try {
        work();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
std::string collect(cymcp::ResourceRegistry &resources, const std::string &uri) {
    auto manifest = resources.read(uri);
    auto metadata = cymcp::parse_json(manifest.at("contents").at(0).at("text"));
    auto next = metadata.at("firstUri").get<std::string>();
    std::string result;
    do {
        auto part = resources.read(next);
        result += part.at("contents").at(0).at("text").get<std::string>();
        next = part.at("_meta").at("com.cymcp/chunk").value("nextUri", std::string());
    } while (!next.empty());
    return result;
}
#ifdef CYSYNCK_ASSISTANT_CHAT_TESTS
class ApiFixture {
    httplib::Server server_;
    std::thread thread_;

  public:
    std::atomic_int requests{0};
    std::atomic_bool remote_error_seen{false};
    std::string url;
    explicit ApiFixture(Json create, std::string remote_tool) {
        server_.Get("/v1/models", [](const auto &, auto &response) {
            response.set_content(Json{{"data", Json::array({{{"id", "fixture"}}})}}.dump(),
                                 "application/json");
        });
        server_.Post("/v1/chat/completions", [this, create, remote_tool](const auto &request,
                                                                         auto &response) {
            ++requests;
            auto input = Json::parse(request.body);
            std::string prompt;
            for (const auto &message : input.at("messages"))
                if (message.at("role") == "user" && message.at("content").is_string())
                    prompt = message.at("content");
            if (prompt == "fail-api") {
                response.status = 503;
                response.set_content("fixture unavailable", "text/plain");
                return;
            }
            bool tool_result = input.at("messages").back().at("role") == "tool";
            if (tool_result &&
                input.at("messages").back().dump().find("RemoteToolError") != std::string::npos)
                remote_error_seen = true;
            std::vector<std::string> chunks;
            auto delta = [](Json data, Json finish = Json()) {
                return "data: " +
                       Json{{"choices",
                             Json::array(
                                 {{{"index", 0}, {"delta", data}, {"finish_reason", finish}}})}}
                           .dump() +
                       "\n\n";
            };
            if ((prompt == "create-api" || prompt == "remote-api" ||
                 prompt == "remote-error-api") &&
                !tool_result) {
                Json call = {{"name", "cysynck.project.create"}, {"arguments", create}};
                if (prompt != "create-api")
                    call = {{"name", remote_tool},
                            {"arguments",
                             {{"name",
                               prompt == "remote-api" ? "cysynck.overview" : "does-not-exist"}}}};
                chunks.push_back(
                    delta({{"tool_calls", Json::array({{{"index", 0},
                                                        {"id", "fixture-call"},
                                                        {"type", "function"},
                                                        {"function",
                                                         {{"name", "cy_tools_call"},
                                                          {"arguments", call.dump()}}}}})}}));
                chunks.push_back(delta(Json::object(), "tool_calls"));
            } else {
                chunks.push_back(delta({{"content", "API streamed "}}));
                if (prompt == "cancel-api")
                    for (int n = 0; n < 15; ++n)
                        chunks.push_back(delta({{"content", "more "}}));
                chunks.push_back(delta({{"content", "complete"}}));
                chunks.push_back(delta(Json::object(), "stop"));
            }
            chunks.push_back("data: [DONE]\n\n");
            auto position = std::make_shared<std::size_t>(0);
            response.set_chunked_content_provider(
                "text/event-stream",
                [chunks = std::move(chunks), position](std::size_t, httplib::DataSink &sink) {
                    if (*position >= chunks.size()) {
                        sink.done();
                        return true;
                    }
                    if (*position)
                        std::this_thread::sleep_for(std::chrono::milliseconds(60));
                    const auto &chunk = chunks[(*position)++];
                    return sink.write(chunk.data(), chunk.size());
                });
        });
        auto port = server_.bind_to_any_port("127.0.0.1");
        if (port <= 0)
            throw std::runtime_error("Cannot bind API fixture");
        url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        while (!server_.is_running())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ~ApiFixture() {
        server_.stop();
        if (thread_.joinable())
            thread_.join();
    }
};
class FixtureProvider : public cymcp::Provider {
    std::shared_ptr<const cymcp::Registry> operations_;
    Json create_;

  public:
    FixtureProvider(std::shared_ptr<const cymcp::Registry> operations, Json create)
        : Provider("cysynck-test"), operations_(std::move(operations)), create_(std::move(create)) {
    }
    bool uses_host_history() const noexcept override { return true; }
    cymcp::ProviderCapabilities capabilities() const override {
        cymcp::ProviderCapabilities c;
        c.streaming = c.tools = c.model_catalogue = true;
        return c;
    }
    void connect(const cymcp::CallContext &) override {}
    Json list_models(const cymcp::CallContext &) override {
        return Json::array({{{"id", "fixture"}}});
    }
    cymcp::Session start_session(std::string model, std::string, const cymcp::CallContext &,
                                 std::string) override {
        return {cymcp::random_token(), model};
    }
    cymcp::ChatResult send(cymcp::Session &, Json &history,
                           const cymcp::CallContext &context) override {
        context.emit("TextDelta", {{"text", "Preparing "}});
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        auto content = history.back().at("content");
        if (content == "cancel")
            for (;;) {
                context.cancellation.check();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        if (content == "create")
            operations_->invoke("cysynck.project.create", create_, context);
        context.emit("TextDelta", {{"text", "complete"}});
        history.push_back({{"role", "assistant"}, {"content", "Preparing complete"}});
        return {"Preparing complete", "stop"};
    }
    void disconnect() noexcept override {}
};
void idle(cymcp::ChatController &controller, bool *streamed = nullptr) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (controller.state().busy) {
        controller.poll();
        if (streamed && controller.state().busy)
            for (auto &message : controller.state().messages)
                if (message.role == "assistant" &&
                    (message.markdown == "Preparing " || message.markdown == "API streamed "))
                    *streamed = true;
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Chat fixture timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    controller.poll();
}
#endif
} // namespace
int main() {
    auto parent = fs::canonical(fs::current_path());
    auto directory = parent / ("assistant-fixture-" + random_id());
    if (fs::exists(directory))
        return 2;
    fs::create_directory(directory);
    struct Cleanup {
        fs::path root, parent;
        ~Cleanup() {
            // Only this new, isolated fixture is ever recursively removed.
            if (root.parent_path() == parent &&
                root.filename().string().starts_with("assistant-fixture-") &&
                !fs::is_symlink(root)) {
                std::error_code error;
                fs::remove_all(root, error);
            }
        }
    } cleanup{directory, parent};
    try {
        fs::create_directory(directory / "project");
        fs::create_directory(directory / "chat-project");
        for (int n = 0; n < 450; ++n)
            std::ofstream(directory / "project" / ("file-" + std::to_string(n) + ".txt"))
                << "fixture content " << n;
        Core core(directory / "profile");
        assistant::Options options;
        options.access_profile = cymcp::LocalAccessProfile::protected_access;
        options.disk_spool = true;
        options.metrics = true;
        options.retention_seconds = 1;
        Host host(core, options);
        check(host.info().at("sdk_version") == "0.7.2", "Source-built SDK version is reported");
        check(host.registry()->size() > 60 && host.tools()->size() == 5,
              "Full registry and compact discovery share the same business operations");
        auto read = host.registry()->invoke("cysynck.overview", Json::object());
        check(read.at("result").at("projects").empty(),
              "Read works against the real empty application profile");
        Json create = {{"parameters",
                        {{"name", "private-marker-not-in-metrics"},
                         {"root", path_string(directory / "project")},
                         {"mode", "backup"}}}};
        rejects([&] { host.registry()->invoke("cysynck.project.create", create); },
                "Protected profile denies writes without its explicit grant");
        check(core.request("GET", "/api/overview").at("projects").empty(),
              "Denied mutation leaves the application unchanged");
        check(host.registry()
                  ->invoke("cysynck.storage.prune", Json::object())
                  .at("result")
                  .contains("plan"),
              "Preview-only calls remain available without write authorization");
        host.allow_writes(true);
        auto created = host.registry()->invoke("cysynck.project.create", create).at("result");
        auto id = created.at("id").get<std::string>();
        auto project_page =
            host.registry()
                ->invoke("cysynck.projects.list", {{"mode", "backup"}, {"limit", 1}})
                .at("result");
        check(project_page.at("projects").size() == 1 && !project_page.at("has_more").get<bool>(),
              "Project catalogue supports filtered bounded pages");
        rejects(
            [&] {
                host.registry()->invoke("cysynck.projects.list", {{"inventory_hash", "stale"}});
            },
            "Pinned project pagination refuses a changed inventory");
        check(core.request("GET", "/api/overview").at("projects").size() == 1,
              "Authorized tool creates a real project");
        cymcp::CallContext stranger;
        stranger.principal = "untrusted";
        rejects([&] { host.registry()->invoke("cysynck.overview", Json::object(), stranger); },
                "Caller-supplied principals cannot bypass the host policy");
        rejects(
            [&] {
                host.registry()->invoke(
                    "cysynck.project.backup-excludes",
                    {{"project_id", id},
                     {"parameters", {{"excludes", created.at("excludes")}, {"plan", "stale"}}}});
            },
            "Native preview-token requirements remain enforced");
        auto snapshot = host.registry()
                            ->invoke("cysynck.project.backup", {{"project_id", id}})
                            .at("result")
                            .at("id");
        auto inventory = host.registry()->invoke("cysynck.project.snapshot-inventory",
                                                 {{"project_id", id}, {"snapshot", snapshot}});
        auto contents = collect(*host.resources(), inventory.at("resource").at("uri"));
        check(std::count(contents.begin(), contents.end(), '\n') == 450,
              "Progressive inventory export retains every paginated file entry");
        auto stored = host.resources()->store_stream(
            [](std::ostream &stream, const cymcp::CallContext &) {
                std::string chunk(8192, 'x');
                for (int n = 0; n < 256; ++n)
                    stream << chunk;
            },
            "text/plain");
        auto uri = stored.at("uri").get<std::string>();
        check(collect(*host.resources(), uri).size() == 2 * 1024 * 1024,
              "Full spooled result is recoverable across every private chunk");
        auto stats = host.resources()->storage_stats();
        check(stats.dump().find("2097152") != std::string::npos,
              "Disk spooling accounts for the large retained result");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        rejects([&] { host.resources()->read(uri); },
                "Expired results are rejected rather than replayed");
        check(core.request("GET", "/api/overview").at("projects").size() == 1,
              "Reading an expired result never replays its mutation");
        auto port = host.listen(0);
        auto url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
        cymcp::McpClient external(url, core.api_token());
        external.connect();
        check(external.list_tools().size() == 5, "External MCP discovers the real compact gateway");
        external.list_tools();
        check(external.cache_stats().hits > 0, "MCP discovery cache is active");
        external.start_notifications(
            {{"toolsListChanged", true},
             {"resourcesListChanged", true},
             {"resourceSubscriptions", Json::array({"cysynck://activity"})}});
        auto activity = external.read_resource("cysynck://activity");
        external.read_resource("cysynck://activity");
        auto outcome = external.call_tool(
            "cy_tools_call", {{"name", "cysynck.overview"}, {"arguments", Json::object()}});
        check(!outcome.value("isError", false), "External MCP reads the real application");
        auto modified = external.call_tool(
            "cy_tools_call",
            {{"name", "cysynck.project.update"},
             {"arguments", {{"project_id", id}, {"parameters", {{"name", "changed-over-mcp"}}}}}});
        check(!modified.value("isError", false) &&
                  core.request("GET", "/api/overview").at("projects").at(0).at("name") ==
                      "changed-over-mcp",
              "Authorized external MCP mutation changes the real application");
        auto notification_deadline = cymcp::Clock::now() + std::chrono::seconds(3);
        while (external.notification_status().value("revision", 0) < 2 &&
               cymcp::Clock::now() < notification_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(external.read_resource("cysynck://activity") != activity,
              "Subscribed activity changes invalidate a cached resource before its TTL");
        check(!external.drain_notifications().empty(),
              "The actual HTTP transport delivers supported change notifications");
        external.stop_notifications();
        host.allow_writes(false);
        auto denied = external.call_tool(
            "cy_tools_call", {{"name", "cysynck.project.create"}, {"arguments", create}});
        check(denied.value("isError", false), "External MCP uses exactly the same write policy");
        cymcp::McpClient unauthenticated(url, "incorrect-token");
        rejects([&] { unauthenticated.connect(); }, "Unauthenticated MCP is rejected");
        host.allow_writes(true);
        Host remote(core);
        remote.connect_remote(url, core.api_token(), {"cy_tools_call"});
        remote.allow_writes(true);
        auto forwarded =
            std::string("remote.") + cymcp::sha256(url).substr(0, 8) + ".cy_tools_call";
        check(!remote.registry()
                   ->invoke(forwarded, {{"name", "cysynck.overview"}})
                   .at("result")
                   .value("isError", false),
              "Explicit remote allowlist forwards an actual application read");
        rejects([&] { remote.registry()->invoke(forwarded, {{"name", "does-not-exist"}}); },
                "Remote business errors are surfaced as failures");
#ifdef CYSYNCK_ASSISTANT_CHAT_TESTS
        auto providers = std::make_shared<cymcp::ProviderRegistry>(false);
        Json chat_create = {{"parameters",
                             {{"name", "chat-created"},
                              {"root", path_string(directory / "chat-project")},
                              {"mode", "backup"}}}};
        providers->add({"fixture", "Test provider", "in-process-test", "none", {}},
                       [operations = host.registry(), chat_create](const cymcp::ProviderConfig &) {
                           return std::make_shared<FixtureProvider>(operations, chat_create);
                       });
        cymcp::ChatConnection connection;
        connection.provider_id = "fixture";
        connection.model = "fixture";
        connection.history_directory = directory / "history";
        connection.history_id = "test";
        connection.context.max_bytes = 1024;
        cymcp::ChatController controller(providers);
        controller.open(connection);
        idle(controller);
        check(controller.state().connected, "Embedded SDK chat connects asynchronously");
        bool streamed = false;
        controller.send("create");
        idle(controller, &streamed);
        check(streamed, "Embedded chat exposes text before the turn completes");
        check(core.request("GET", "/api/overview").at("projects").size() == 2,
              "Chat tool call acts on the real application state");
        controller.send("cancel");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        controller.cancel();
        idle(controller);
        check(!controller.state().busy, "Embedded chat generation is cancellable");
        controller.disconnect();
        idle(controller);
        connection.restore = true;
        controller.open(connection);
        idle(controller);
        check(controller.state().connected && !controller.state().messages.empty(),
              "Private chat history restores after reconnection");
        fs::create_directory(directory / "api-project");
        Json api_create = {{"parameters",
                            {{"name", "api-created"},
                             {"root", path_string(directory / "api-project")},
                             {"mode", "backup"}}}};
        ApiFixture api(api_create, forwarded);
        cymcp::ChatController api_chat;
        cymcp::ChatConnection api_connection;
        api_connection.provider_id = "openai-compatible";
        api_connection.model = "fixture";
        api_connection.provider.settings = {{"base_url", api.url}, {"deferred_tools", true}};
        api_connection.provider.tools = host.tools();
        api_connection.history_directory = directory / "api-history";
        api_connection.history_id = "api";
        api_connection.context.max_bytes = 2048;
        api_chat.open(api_connection);
        idle(api_chat);
        check(api_chat.state().connected,
              "Real SDK API transport connects to a local deterministic HTTP fixture");
        streamed = false;
        api_chat.send("create-api");
        idle(api_chat, &streamed);
        check(core.request("GET", "/api/overview").at("projects").size() == 3 &&
                  api_chat.state().error_code.empty(),
              "API SSE tool exchange executes a genuine CySynck operation");
        check(streamed, "API text reaches the embedded chat while generation is still active");
        check(api.requests == 2,
              "One API tool exchange uses two requests and never retries its mutation");
        cymcp::ChatController remote_chat;
        auto remote_connection = api_connection;
        remote_connection.provider.tools = remote.tools();
        remote_connection.history_id = "remote";
        remote_connection.context.max_bytes = 32768;
        remote_chat.open(remote_connection);
        idle(remote_chat);
        remote_chat.send("remote-api");
        idle(remote_chat);
        if (!remote_chat.state().error_code.empty())
            std::cerr << "Remote chat: " << remote_chat.state().error_code << ' '
                      << remote_chat.state().error_message << '\n';
        check(remote_chat.state().error_code.empty(),
              "A remote MCP tool can be invoked from the actual API-backed embedded chat");
        remote_chat.send("remote-error-api");
        idle(remote_chat);
        check(api.remote_error_seen,
              "Remote MCP tool errors reach the model as an explicit structured tool failure");
        for (int n = 0; n < 5; ++n) {
            api_chat.send(std::string(600, 'x'));
            idle(api_chat);
        }
        auto api_store = cymcp::ConversationStore(directory / "api-history").load("api");
        check(api_chat.state().messages.size() >= 12 && api_store.dump().size() > 2048,
              "Context projection preserves the complete durable application history");
        auto requests = api.requests.load();
        api_chat.send("fail-api");
        idle(api_chat);
        check(!api_chat.state().error_code.empty() && api.requests == requests + 1,
              "API failures are surfaced without an automatic generation retry");
        api_chat.send("cancel-api");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        api_chat.cancel();
        idle(api_chat);
        check(!api_chat.state().busy, "Actual API streaming is cancellable");
        fs::create_directory(directory / "free-chat-project");
        Json free_create = {{"parameters",
                             {{"name", "free-chat-created"},
                              {"root", path_string(directory / "free-chat-project")},
                              {"mode", "backup"},
                              {"backup_interval_minutes", 0}}}};
        Host free_host(core);
        ApiFixture free_api(free_create, forwarded);
        cymcp::ChatController free_chat;
        auto free_connection = api_connection;
        free_connection.provider.settings["base_url"] = free_api.url;
        free_connection.provider.tools = free_host.tools();
        free_connection.history_id = "free";
        free_connection.context.max_bytes = 32768;
        free_chat.open(free_connection);
        idle(free_chat);
        free_chat.send("create-api");
        idle(free_chat);
        bool free_created = false;
        auto free_overview = core.request("GET", "/api/overview");
        for (const auto &p : free_overview.at("projects"))
            if (p.at("name") == "free-chat-created")
                free_created = true;
        check(free_host.writes_allowed() && free_created && free_chat.state().error_code.empty(),
              "Free chat executes a real tool mutation without an additional write grant");
        check(free_api.requests == 2,
              "Free chat performs one mutation without confirmation or automatic retry");
#endif
        auto measurements =
            read_file(directory / "profile/assistant-metrics.jsonl", 2 * 1024 * 1024);
        check(measurements.find("private-marker-not-in-metrics") == std::string::npos &&
                  measurements.find(core.api_token()) == std::string::npos &&
                  measurements.find(path_string(directory / "project")) == std::string::npos,
              "Metrics exclude names, paths, arguments, results and secrets");
        std::cout << "PASS " << checks
                  << " CySynck assistant checks; isolated fixture removed on exit\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << ": " << cymcp::redact_error_message(e.what())
                  << '\n';
        return 1;
    }
}
