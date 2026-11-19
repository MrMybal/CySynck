#include "assistant.hpp"
#include "assistant_schemas.hpp"
#include "error.hpp"
#include <algorithm>
#include <cctype>
#include <csignal>
#include <fstream>
#include <iostream>
#include <set>

namespace cysynck::assistant {
namespace {
Json text_schema(std::string description = {}) {
    return {{"type", "string"}, {"maxLength", 8192}, {"description", description}};
}
Json object_schema(Json properties = Json::object(), Json required = Json::array(),
                   bool extra = false) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", extra}};
}
Json redact(Json value) {
    if (value.is_object()) {
        for (auto &[key, item] : value.items()) {
            auto lower = key;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "api_token" || lower == "api_key" || lower == "apikey" ||
                lower == "private_key" || lower == "privatekey" || lower == "mesh_token" ||
                lower == "secret" || lower == "authorization")
                item = "[redacted]";
            else
                item = redact(std::move(item));
        }
    } else if (value.is_array())
        for (auto &item : value)
            item = redact(std::move(item));
    return value;
}
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
int result_exit_code(const Json &value) {
    if (value.is_object() && value.contains("result") && value.at("result").is_object()) {
        const auto &result = value.at("result");
        if (result.contains("ok") && result.at("ok").is_boolean() && !result.at("ok").get<bool>()) {
            std::cerr
                << Json{{"error",
                         {{"code", "BusinessCheckFailed"},
                          {"message",
                           "The operation reported ok=false; inspect the JSON report on stdout"}}}}
                       .dump()
                << '\n';
            return 4;
        }
    }
    return 0;
}
} // namespace

Json Host::sanitize_result(Json result) { return {{"result", redact(std::move(result))}}; }

Options Options::load(const fs::path &state) {
    Options result;
    auto file = state / "assistant-options.json";
    if (!fs::exists(file))
        return result;
    auto j = cymcp::parse_json(read_file(file, 16384));
    result.compact = j.value("compact_discovery", true);
    result.notifications = j.value("notifications", true);
    result.disk_spool = j.value("disk_spool", false);
    result.metrics = j.value("metrics", false);
    result.inline_bytes = j.value("inline_bytes", 16384U);
    result.spool_quota_mib = j.value("spool_quota_mib", 128U);
    result.context_bytes = j.value("context_bytes", 0U);
    result.cache_ttl_ms = j.value("cache_ttl_ms", 30000);
    result.retention_seconds = j.value("retention_seconds", 300);
    if (result.inline_bytes < 1024 || result.inline_bytes > 1024 * 1024 ||
        result.spool_quota_mib < 1 || result.spool_quota_mib > 1024 ||
        result.context_bytes > 16 * 1024 * 1024 || result.cache_ttl_ms < 0 ||
        result.cache_ttl_ms > 300000 || result.retention_seconds < 1 ||
        result.retention_seconds > 86400)
        throw cymcp::Error("InvalidOptions",
                           "Invalid assistant resource, retention, cache or context limit");
    return result;
}

Host::Host(Core &core, Options options, bool persistent, std::shared_ptr<DesktopBridge> desktop)
    : core_(core), options_(options), persistent_(persistent), desktop_(std::move(desktop)) {
    auto policy = cymcp::Policy(
        [this](const cymcp::CallContext &context, const std::string &name, const Json &arguments) {
            if (context.principal != "local")
                return cymcp::Decision::deny;
            auto description = registry_->describe(name);
            return description.at("annotations").value("readOnlyHint", false) ||
                           schemas::preview(name, arguments) || writes_.load()
                       ? cymcp::Decision::allow
                       : cymcp::Decision::deny;
        });
    registry_ = std::make_shared<cymcp::Registry>(policy);
    if (options.notifications) {
        changes_ = std::make_shared<cymcp::ChangeFeed>();
        registry_->set_change_feed(changes_);
    }
    if (options.metrics) {
        cymcp::PerformanceOptions performance;
        performance.jsonl_file = core.state() / "assistant-metrics.jsonl";
        performance_ = std::make_shared<cymcp::PerformanceMonitor>(performance);
        registry_->set_performance_monitor(performance_);
    }
    register_operations();
    register_extended_operations();
    cymcp::ToolGatewayOptions gateway;
    gateway.inline_bytes = options.inline_bytes;
    gateway.resources.max_stored_bytes = 16 * 1024 * 1024;
    gateway.resources.result_ttl = std::chrono::seconds(options.retention_seconds);
    gateway.resources.max_disk_bytes = options.spool_quota_mib * 1024 * 1024;
    if (options.disk_spool)
        gateway.resources.disk_directory = core.state() / "assistant-results";
    // Result/resource policies use private principal ownership and do not expose
    // arbitrary application files. ToolGateway checks the business policy again.
    resources_ = std::make_shared<cymcp::ResourceRegistry>(
        cymcp::Policy([](const cymcp::CallContext &c, const std::string &, const Json &) {
            return c.principal == "local" ? cymcp::Decision::allow : cymcp::Decision::deny;
        }),
        gateway.resources);
    resources_->set_change_feed(changes_);
    auto help =
        std::make_shared<const std::string>(cli(core_, {"help"}).at("help").get<std::string>());
    cymcp::Resource resource;
    resource.uri = "cysynck://help/cli";
    resource.name = "CySynck command reference";
    resource.description = "Existing application commands and preview/confirmation requirements. "
                           "Search with cy_resources_grep.";
    resource.size = help->size();
    resource.read = [help](std::size_t offset, std::size_t length, const cymcp::CallContext &c) {
        c.cancellation.check();
        return help->substr(offset, length);
    };
    resources_->add(std::move(resource));
    observe(core_.request("GET", "/api/events"));
    cymcp::Operation inventory;
    inventory.name = "cysynck.project.snapshot-inventory";
    inventory.description = "Stream an entire saved snapshot file inventory into a private "
                            "retained JSONL resource, using pinned 100-file pages. Follow its "
                            "chunk URIs; an expired reference never replays an operation.";
    inventory.read_only = true;
    inventory.input_schema = object_schema(
        {{"project_id", text_schema()}, {"snapshot", text_schema()}, {"prefix", text_schema()}},
        {"project_id", "snapshot"});
    inventory.output_schema = object_schema({{"resource", {{"type", "object"}}}}, {"resource"});
    inventory.handler = [this](const Json &args, const cymcp::CallContext &context) {
        auto project = args.at("project_id").get<std::string>();
        if (project.empty() || !std::all_of(project.begin(), project.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_';
            }))
            throw cymcp::Error("InvalidArguments", "Invalid project ID");
        return Json{{"resource",
                     resources_->store_stream(
                         [&](std::ostream &output, const cymcp::CallContext &call) {
                             Json query = {{"snapshot", args.at("snapshot")},
                                           {"prefix", args.value("prefix", "")},
                                           {"offset", 0},
                                           {"limit", 100}};
                             for (;;) {
                                 call.cancellation.check();
                                 auto page = core_.request(
                                     "POST", "/api/projects/" + project + "/snapshot-files", query);
                                 for (const auto &file : page.at("files"))
                                     output << file.dump() << '\n';
                                 if (!page.at("has_more").get<bool>())
                                     break;
                                 if (page.at("files").empty())
                                     throw cymcp::Error("InvalidOutput",
                                                        "Snapshot pagination made no progress");
                                 query["manifest_hash"] = page.at("manifest_hash");
                                 query["offset"] =
                                     page.at("offset").get<std::size_t>() + page.at("files").size();
                             }
                         },
                         "application/x-ndjson", context)}};
    };
    registry_->add(std::move(inventory));
    cymcp::Operation projects;
    projects.name = "cysynck.projects.list";
    projects.description =
        "List projects with optional name_contains/mode filters and bounded offset/limit. Pin "
        "inventory_hash from the first page to detect concurrent changes.";
    projects.read_only = true;
    projects.input_schema =
        object_schema({{"name_contains", text_schema()},
                       {"mode", text_schema()},
                       {"inventory_hash", text_schema()},
                       {"offset", {{"type", "integer"}, {"minimum", 0}}},
                       {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}});
    projects.output_schema = object_schema({{"result", {{"type", "object"}}}}, {"result"});
    projects.handler = [this](const Json &args, const cymcp::CallContext &context) {
        context.cancellation.check();
        auto all = core_.request("GET", "/api/overview").at("projects");
        auto hash = cymcp::sha256(all.dump());
        if (args.contains("inventory_hash") && args.at("inventory_hash") != hash)
            throw cymcp::Error("InventoryChanged",
                               "Project inventory changed; refresh the first page");
        Json selected = Json::array();
        auto name = args.value("name_contains", std::string()),
             mode = args.value("mode", std::string());
        for (const auto &project : all)
            if (project.at("name").get<std::string>().find(name) != std::string::npos &&
                (mode.empty() || project.at("mode") == mode))
                selected.push_back(project);
        auto offset = args.value("offset", std::size_t{0}),
             limit = args.value("limit", std::size_t{100});
        Json page = Json::array();
        for (auto n = offset; n < selected.size() && page.size() < limit; ++n)
            page.push_back(selected.at(n));
        return Json{{"result",
                     {{"projects", redact(std::move(page))},
                      {"offset", offset},
                      {"total", selected.size()},
                      {"inventory_hash", hash},
                      {"has_more", offset < selected.size() && limit < selected.size() - offset}}}};
    };
    registry_->add(std::move(projects));
    gateway_ = std::make_unique<cymcp::ToolGateway>(registry_, gateway, resources_);
    watch_activity();
}
Host::~Host() {
    if (desktop_)
        desktop_->close();
    if (activity_watcher_.joinable())
        activity_watcher_.request_stop();
    stop();
    if (activity_watcher_.joinable())
        activity_watcher_.join();
    receiver_action("stop");
}
std::shared_ptr<const cymcp::Registry> Host::tools(bool full) const {
    return full || !options_.compact ? registry_ : gateway_->tools();
}
Json Host::info() const {
    return {{"sdk_version", cymcp::version},
            {"sdk_revision", CYSYNCK_CYMCP_REVISION},
            {"operations", registry_->size()},
            {"writes_allowed", writes_.load()},
            {"persistent_instance", persistent_.load()},
            {"desktop_actions", bool(desktop_)},
            {"compact_discovery", options_.compact},
            {"notifications", options_.notifications},
            {"disk_spool", options_.disk_spool},
            {"retention_seconds", options_.retention_seconds},
            {"spool_quota_mib", options_.spool_quota_mib},
            {"cache_ttl_ms", options_.cache_ttl_ms},
            {"metrics", options_.metrics},
            {"context_bytes", options_.context_bytes},
            {"transports", {"stdio", "authenticated-loopback-http-json"}}};
}
void Host::changed() {
    // Optional observation must not turn a committed mutation into a failure.
    try {
        observe(core_.request("GET", "/api/events"));
    } catch (const std::exception &) {
    }
}
void Host::observe(const Json &events) {
    auto snapshot = std::make_shared<const std::string>(redact(events).dump());
    auto digest = cymcp::sha256(*snapshot);
    std::lock_guard lock(observation_mutex_);
    if (digest == activity_digest_)
        return;
    cymcp::Resource resource;
    resource.uri = "cysynck://activity";
    resource.name = "CySynck activity snapshot";
    resource.description = "Immutable snapshot of the application activity journal; updated by "
                           "registry mutations and desktop refresh.";
    resource.mime_type = "application/json";
    resource.size = snapshot->size();
    resource.cache_ttl = std::chrono::milliseconds(options_.cache_ttl_ms);
    resource.read = [snapshot](std::size_t offset, std::size_t length,
                               const cymcp::CallContext &context) {
        context.cancellation.check();
        return snapshot->substr(offset, length);
    };
    if (activity_digest_.empty())
        resources_->add(std::move(resource));
    else
        resources_->replace(std::move(resource));
    activity_digest_ = std::move(digest);
}
void Host::add(std::string name, std::string description, std::string method, std::string route,
               bool read_only, Json properties, Json required, bool project) {
    if (properties.empty()) {
        auto parameters = schemas::parameters(name);
        if (!parameters.at("required").empty())
            required.push_back("parameters");
        properties["parameters"] = std::move(parameters);
    }
    if (project) {
        properties["project_id"] = {{"type", "string"},
                                    {"minLength", 1},
                                    {"maxLength", 128},
                                    {"description", "Existing project ID from cysynck.overview"}};
        required.push_back("project_id");
    }
    cymcp::Operation operation;
    operation.name = std::move(name);
    operation.description = std::move(description);
    operation.input_schema = object_schema(properties, required);
    operation.output_schema = schemas::output(operation.name);
    operation.read_only = read_only;
    operation.handler = [this, method, route, project,
                         read_only](const Json &args, const cymcp::CallContext &context) {
        context.cancellation.check();
        auto path = route;
        if (project) {
            auto id = args.at("project_id").get<std::string>();
            if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_';
                }))
                throw cymcp::Error("InvalidArguments", "Invalid project ID");
            path.replace(path.find("{project}"), 9, id);
        }
        auto result = core_.request(method, path, args.value("parameters", Json::object()));
        if (!read_only)
            changed();
        // Do not throw cancellation after a committed business request: this
        // response is its receipt, and an interrupted caller must never replay it.
        return Json{{"result", redact(std::move(result))}};
    };
    registry_->add(std::move(operation));
}
void Host::register_operations() {
    auto global = [this](const char *name, const char *desc, const char *method, const char *route,
                         bool read) { add(name, desc, method, route, read); };
    global("cysynck.overview",
           "Inspect projects, modes and backup summaries before choosing an action.", "GET",
           "/api/overview", true);
    global("cysynck.events",
           "Read the application activity journal to inspect completed operations and errors.",
           "GET", "/api/events", true);
    global("cysynck.storage.status",
           "Inspect backup storage and retention before considering pruning.", "GET",
           "/api/storage", true);
    global("cysynck.plugins",
           "Describe installed plugins and their actual capabilities; does not load code.", "GET",
           "/api/plugins", true);
    global("cysynck.doctor",
           "Inspect runtime requirements. parameters.probe=true explicitly runs configured "
           "diagnostic probes.",
           "POST", "/api/doctor", true);
    global("cysynck.engine.status", "Inspect the real Syncthing engine and managed process status.",
           "GET", "/api/engine/status", true);
    global("cysynck.engine.devices", "Inspect Syncthing device configuration.", "GET",
           "/api/engine/devices", true);
    global("cysynck.engine.log", "Inspect the managed engine log.", "GET", "/api/engine/log", true);
    global("cysynck.network.status",
           "Inspect network and VPN status; private credentials are removed.", "GET",
           "/api/network/status", true);
    global("cysynck.network.history", "Read direct chat history using parameters.project.", "POST",
           "/api/network/history", true);
    global("cysynck.transfers.limits", "Inspect the configured direct transfer quotas.", "GET",
           "/api/network/transfer-limits", true);
    global("cysynck.transfers.list", "Inspect resumable direct transfers using parameters.project.",
           "POST", "/api/network/transfers", true);
    global("cysynck.project.create",
           "Create a real project with an existing root. Requires local write authorization. Never "
           "places the profile inside the project.",
           "POST", "/api/projects", false);
    global("cysynck.storage.prune",
           "Preview or apply backup pruning. Applying requires parameters.apply=true and the exact "
           "plan token from its preview, plus write authorization.",
           "POST", "/api/storage/prune", false);
    // Every route below is a real Core action. Its native validation and durable
    // receipts are preserved, rather than reconstructed in an AI adapter.
    struct Action {
        const char *name;
        const char *description;
        bool read;
    };
    for (const auto &action :
         {Action{"backup-preview",
                 "Preview backup scope and pagination before backing up or changing exclusions.",
                 true},
          {"backup",
           "Create a real backup using the saved rules; a supplied plan must still match.", false},
          {"changes", "Inspect changes against a saved snapshot.", true},
          {"diff", "Read a file difference against a snapshot using parameters.path.", true},
          {"compare", "Compare two project files without changing them.", true},
          {"conflicts", "List synchronization conflicts.", true},
          {"conflict-diff", "Read a conflict difference before selecting a resolution.", true},
          {"resolution-plan", "Preview conflict recovery and obtain its exact approval token.",
           true},
          {"resolve-conflict",
           "Apply a reviewed conflict resolution with its current plan token; preserves recovery "
           "files.",
           false},
          {"resolutions", "Inspect durable conflict recovery receipts.", true},
          {"resolution-export", "Export verified conflict recovery files to a new directory.",
           false},
          {"resolution-acknowledge",
           "Mark an interrupted recovery as reviewed without deleting its files.", false},
          {"verify", "Verify backup content hashes without applying a restore.", true},
          {"snapshot-files",
           "List snapshot files with prefix, offset, limit and inventory consistency checks.",
           true},
          {"snapshot-compare", "Compare two snapshots with bounded pagination.", true},
          {"snapshot-compare-file", "Read one difference between saved snapshots.", true},
          {"restore-plan", "Preview restoration to a new directory and obtain its plan token.",
           true},
          {"restore",
           "Restore to a new directory with the reviewed plan; existing destination data is "
           "protected.",
           false},
          {"git-status", "Inspect Git exchange state without modifying the working repository.",
           true},
          {"git-branches", "List verified peer branches.", true},
          {"inbox", "Inspect incoming Git exchange bundles.", true},
          {"export",
           "Publish a Git exchange bundle; optional LFS remains subject to project policy.", false},
          {"import", "Verify and import a selected peer Git bundle.", false},
          {"git-review", "Review a verified peer branch before integration.", true},
          {"git-review-file", "Read a committed peer file difference.", true},
          {"git-history", "Read paginated committed peer history.", true},
          {"git-commit", "Inspect one commit and its selected parent.", true},
          {"git-commit-file", "Read one committed file change.", true},
          {"git-merge-review", "Review a three-way Git merge without applying it.", true},
          {"git-merge-file",
           "Prepare an editable per-file merge draft; never writes the working tree.", true},
          {"git-lfs-cache", "Inspect the verified local LFS cache with filters and pagination.",
           true},
          {"git-integrations", "List durable Git integration receipts.", true},
          {"git-integration-check",
           "Verify a durable integration receipt before recovery or acknowledgement.", true},
          {"git-materializations", "List durable asset materialization receipts.", true},
          {"git-materialization-check", "Verify retained materialization recovery data.", true},
          {"git-lfs-recoveries", "List durable LFS refill receipts.", true},
          {"git-lfs-recovery", "Inspect one durable LFS refill receipt.", true},
          {"git-lfs-recovery-check", "Verify all objects in an LFS refill receipt.", true},
          {"received-files", "List durably indexed direct file receipts.", true},
          {"received-verify", "Verify the full hash of a received file.", true},
          {"received-export", "Export a verified received file into a new destination.", false},
          {"register", "Register a project with Syncthing, preserving Git/recovery exclusions.",
           false},
          {"sync", "Request synchronization through the actual configured project mode.", false},
          {"status", "Inspect the actual project synchronization status.", true},
          {"pause", "Pause synchronization for this project.", false},
          {"resume", "Resume synchronization for this project.", false},
          {"git-setup", "Preview or apply Git setup; apply requires its current plan token.",
           false},
          {"git-extract",
           "Preview or extract reviewed Git assets to a new directory; apply requires its plan.",
           false},
          {"git-integrate",
           "Preview or integrate a verified branch; preserves existing local content and requires "
           "a plan to apply.",
           false},
          {"git-merge",
           "Preview or apply a reviewed three-way merge with explicit resolutions and its plan.",
           false},
          {"git-materialize", "Preview or materialize verified cached LFS assets with a plan.",
           false},
          {"git-materialization-resume",
           "Resume a durable interrupted materialization only after a fresh preview.", false},
          {"git-materialization-export",
           "Preview or export retained recovery assets to a new directory.", false},
          {"git-materialization-acknowledge",
           "Acknowledge reviewed recovery using the latest check token.", false},
          {"git-lfs-recover", "Preview or refill verified LFS objects from an authorized peer.",
           false},
          {"git-lfs-recovery-acknowledge",
           "Acknowledge reviewed LFS recovery using the latest check token.", false},
          {"git-integration-acknowledge",
           "Acknowledge reviewed integration recovery using the latest check token.", false},
          {"git-dehydrate",
           "Preview or replace verified clean LFS assets with original pointers; applying requires "
           "a plan.",
           false},
          {"git-storage",
           "Preview or prune project Git exchange storage with its exact plan token.", false}})
        add(std::string("cysynck.project.") + action.name, action.description, "POST",
            std::string("/api/projects/{project}/") + action.name, action.read, {}, {}, true);
    add("cysynck.project.snapshots",
        "List backup snapshots; individual file inventories have separate paginated operations.",
        "GET", "/api/projects/{project}/snapshots", true, {}, {}, true);
    add("cysynck.project.cyrevision-status",
        "Inspect the project's CyRevision compatibility configuration and public state without "
        "changing trust.",
        "GET", "/api/projects/{project}/cyrevision-status", true, {}, {}, true);
    add("cysynck.project.cyrevision-inbox",
        "Inspect incoming signed CyRevision transactions; acceptance and trust remain explicit "
        "native workflows.",
        "GET", "/api/projects/{project}/cyrevision-inbox", true, {}, {}, true);
    add("cysynck.project.update",
        "Update a project's native settings. Requires explicit write authorization.", "PUT",
        "/api/projects/{project}", false, {}, {}, true);
    add("cysynck.project.mode-options",
        "Read the active plugin's settings schema and its current plan token.", "GET",
        "/api/projects/{project}/mode-options", true, {}, {}, true);
    add("cysynck.project.mode-options-set",
        "Save plugin settings using parameters.values and the current parameters.plan.", "PUT",
        "/api/projects/{project}/mode-options", false, {}, {}, true);
    add("cysynck.project.backup-excludes",
        "Save reviewed backup exclusions using parameters.excludes and its preview plan.", "PUT",
        "/api/projects/{project}/backup-excludes", false, {}, {}, true);
}
Json Host::command(const std::vector<std::string> &args) {
    if (args.empty() || args[0] == "help" || args[0] == "--help")
        return {
            {"help",
             "cysynck [--state DIR] tools list | describe NAME | call NAME JSON | call NAME @FILE "
             "| info\ncysynck [--state DIR] tools call NAME JSON --allow-write\ncysynck [--state "
             "DIR] tools list|describe|call ... --endpoint http://127.0.0.1:PORT\ncysynck [--state "
             "DIR] mcp --stdio [--full] [--allow-write]\ncysynck [--state DIR] mcp --port PORT "
             "[--full] [--allow-write]\ncysynck tools read-resource URI --endpoint "
             "http://127.0.0.1:PORT\nHTTP: bearer token from CYSYNCK_API_TOKEN or the selected "
             "profile api-token; /mcp JSON-RPC or /v1 native dispatch. Read-only by "
             "default.\nOptions: profile/assistant-options.json. Existing CLI commands remain "
             "available."}};
    if (args[0] == "list" && args.size() == 1)
        return registry_->describe();
    if (args[0] == "info" && args.size() == 1)
        return info();
    if (args[0] == "describe" && args.size() == 2)
        return registry_->describe(args[1]);
    if (args[0] == "call" && args.size() == 3) {
        auto input = args[2].starts_with('@')
                         ? read_file(fs::u8path(args[2].substr(1)), 1024 * 1024)
                         : args[2];
        return registry_->invoke(args[1], cymcp::parse_json(input));
    }
    throw cymcp::Error("InvalidArguments", "Use cysynck tools help");
}
void Host::stdio(bool full) {
    persistent_.store(true);
    cymcp::McpServerOptions options;
    options.resources = resources_;
    options.changes = changes_;
    options.performance = performance_;
    options.cache_ttl = std::chrono::milliseconds(options_.cache_ttl_ms);
    server_ = std::make_unique<cymcp::McpServer>(*tools(full), "CySynck", options);
    watch_activity();
    server_->serve_stdio(std::cin, std::cout);
}
int Host::listen(int port, bool full) {
    persistent_.store(true);
    if (http_)
        throw cymcp::Error("InvalidState", "The assistant MCP endpoint is already running");
    cymcp::McpServerOptions options;
    options.resources = resources_;
    options.changes = changes_;
    options.performance = performance_;
    options.cache_ttl = std::chrono::milliseconds(options_.cache_ttl_ms);
    server_ = std::make_unique<cymcp::McpServer>(*tools(full), "CySynck", options);
    http_ = std::make_unique<cymcp::LocalHttpServer>(
        core_.api_token(), [this](const Json &request, const cymcp::CallContext &context) {
            // The transport has already authenticated the profile bearer token.
            // All three entry points represent this same local application account;
            // no caller-provided JSON can choose a principal or elevate its rights.
            auto application_context = context;
            application_context.principal = "local";
            if (!request.contains("jsonrpc") &&
                request.value("method", std::string()) == "read-resource")
                return resources_->read(request.at("uri"), application_context);
            return request.contains("jsonrpc")
                       ? server_->handle(request, application_context)
                       : cymcp::dispatch_app(*registry_, request, application_context);
        });
    try {
        auto bound = http_->start(port);
        watch_activity();
        return bound;
    } catch (...) {
        http_.reset();
        server_.reset();
        throw;
    }
}
void Host::watch_activity() {
    if (activity_watcher_.joinable())
        return;
    activity_watcher_ = std::jthread([this](std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        while (!stop.stop_requested()) {
            try {
                observe(core_.request("GET", "/api/events"));
            } catch (const std::exception &) {
            }
            std::vector<std::shared_ptr<cymcp::McpClient>> clients;
            {
                std::lock_guard lock(remotes_mutex_);
                clients = remotes_;
            }
            // SDK readers invalidate their caches internally. Drain their bounded
            // metadata queues on this host worker, never on the rendering thread.
            for (const auto &client : clients) {
                try {
                    (void)client->drain_notifications();
                } catch (const std::exception &) {
                }
            }
            std::unique_lock lock(mutex);
            wake.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
        }
    });
}
void Host::stop() {
    if (server_)
        server_->cancel_all();
    if (http_)
        http_->stop();
    http_.reset();
    server_.reset();
}
void Host::connect_remote(const std::string &url, const std::string &token,
                          const std::vector<std::string> &allowed,
                          const cymcp::Cancellation &cancellation) {
    if (allowed.empty())
        throw cymcp::Error("InvalidOptions", "Explicitly select remote tools before connecting");
    cymcp::McpClientOptions options;
    options.max_cache_ttl = std::chrono::milliseconds(options_.cache_ttl_ms);
    options.performance = performance_;
    auto remote = std::make_shared<cymcp::McpClient>(url, token, options);
    remote->connect(cancellation);
    if (options_.notifications) {
        try {
            remote->start_notifications(
                {{"toolsListChanged", true}, {"resourcesListChanged", true}}, cancellation);
        } catch (const cymcp::Error &error) {
            if (error.code != "Unsupported")
                throw;
        }
    }
    const auto prefix = "remote." + cymcp::sha256(url).substr(0, 8) + ".";
    // Fetch every requested schema first, so a failed discovery registers no tools.
    std::vector<cymcp::Operation> operations;
    for (const auto &name : allowed) {
        auto schema = remote->get_tool(name, cancellation);
        cymcp::Operation operation;
        operation.name = prefix + name;
        operation.description =
            "User-authorized external tool: " + schema.value("description", name);
        operation.input_schema = schema.at("inputSchema");
        operation.output_schema = object_schema({{"result", Json::object()}}, {"result"});
        operation.read_only = false; // external annotations do not grant local write permission
        operation.handler = [remote, name, token](const Json &args,
                                                  const cymcp::CallContext &context) {
            auto result = remote->call_tool(name, args, context.cancellation);
            remote->clear_cache(); // A remote mutation/error may have changed its resources.
            if (result.value("isError", false)) {
                std::string details = "The external MCP tool returned an error";
                for (const auto &block : result.value("content", Json::array()))
                    if (block.is_object() && block.value("type", std::string()) == "text" &&
                        block.contains("text") && block.at("text").is_string()) {
                        details += ": " + block.at("text").get<std::string>().substr(0, 4096);
                        break;
                    }
                throw cymcp::Error("RemoteToolError",
                                   cymcp::redact_error_message(details, {token}));
            }
            return Json{{"result", std::move(result)}};
        };
        if (std::any_of(operations.begin(), operations.end(),
                        [&](const auto &existing) { return existing.name == operation.name; }))
            throw cymcp::Error("InvalidOptions", "Duplicate remote tool selection");
        operations.push_back(std::move(operation));
    }
    cymcp::Operation list;
    list.name = prefix + "resources.list";
    list.description = "List resources of the explicitly connected external application; the MCP "
                       "client follows supported discovery pagination.";
    list.input_schema = object_schema();
    list.output_schema = object_schema({{"result", Json::object()}}, {"result"});
    list.read_only = true;
    list.handler = [remote](const Json &, const cymcp::CallContext &context) {
        return Json{{"result", remote->list_resources(context.cancellation)}};
    };
    operations.push_back(std::move(list));
    cymcp::Operation read;
    read.name = prefix + "resources.read";
    read.description = "Read a resource URI returned by the explicitly connected application, "
                       "including retained-result chunks.";
    read.input_schema = object_schema({{"uri", text_schema()}}, {"uri"});
    read.output_schema = object_schema({{"result", Json::object()}}, {"result"});
    read.read_only = true;
    read.handler = [remote](const Json &args, const cymcp::CallContext &context) {
        return Json{{"result", remote->read_resource(args.at("uri"), context.cancellation)}};
    };
    operations.push_back(std::move(read));
    cymcp::Operation clear;
    clear.name = prefix + "cache.clear";
    clear.description = "Explicitly refresh cached discovery/resources of this configured external "
                        "application; never replays a tool call";
    clear.input_schema = object_schema();
    clear.output_schema = object_schema({{"result", {{"type", "object"}}}}, {"result"});
    clear.read_only = true;
    clear.handler = [remote](const Json &, const cymcp::CallContext &context) {
        context.cancellation.check();
        remote->clear_cache();
        return Json{{"result", {{"cleared", true}}}};
    };
    operations.push_back(std::move(clear));
    cymcp::Registry validation(cymcp::Policy::deny_all());
    for (const auto &operation : operations) {
        validation.add(operation);
        auto catalogue = registry_->describe();
        if (std::any_of(catalogue.begin(), catalogue.end(), [&](const Json &existing) {
                return existing.at("name") == operation.name;
            }))
            throw cymcp::Error("InvalidOptions", "This remote tool is already connected");
    }
    cancellation.check();
    for (auto &operation : operations)
        registry_->add(std::move(operation));
    std::lock_guard lock(remotes_mutex_);
    remotes_.push_back(std::move(remote));
}
bool handles(const std::vector<std::string> &args) {
    return !args.empty() && (args[0] == "tools" || args[0] == "mcp");
}
int run(Core &core, const std::vector<std::string> &arguments) {
    try {
        auto args = arguments;
        bool writes = false, full = false;
        std::string endpoint;
        for (std::size_t i = 0; i < args.size(); ++i)
            if (args[i] == "--endpoint") {
                if (i + 1 == args.size() || !endpoint.empty())
                    throw cymcp::Error("InvalidArguments", "Supply one --endpoint URL");
                endpoint = args[i + 1];
                args.erase(args.begin() + i, args.begin() + i + 2);
                break;
            }
        args.erase(std::remove_if(args.begin(), args.end(),
                                  [&](const std::string &arg) {
                                      if (arg == "--allow-write") {
                                          writes = true;
                                          return true;
                                      }
                                      if (arg == "--full") {
                                          full = true;
                                          return true;
                                      }
                                      return false;
                                  }),
                   args.end());
        if (!endpoint.empty()) {
            auto url = cymcp::parse_url(endpoint);
            if (args[0] != "tools" || !url.loopback || url.target != "/")
                throw cymcp::Error("InvalidArguments",
                                   "--endpoint selects a loopback CySynck MCP origin URL");
            if (writes)
                throw cymcp::Error("InvalidArguments",
                                   "Authorize writes in the running instance; --allow-write cannot "
                                   "elevate a remote call");
            Json request;
            if (args.size() == 3 && args[1] == "read-resource")
                request = {{"method", "read-resource"}, {"uri", args[2]}};
            else if (args.size() == 2 && args[1] == "list")
                request = {{"method", "describe"}};
            else if (args.size() == 3 && args[1] == "describe")
                request = {{"method", "describe"}};
            else if (args.size() == 4 && args[1] == "call") {
                auto input = args[3].starts_with('@')
                                 ? read_file(fs::u8path(args[3].substr(1)), 1024 * 1024)
                                 : args[3];
                request = {{"method", "invoke"},
                           {"name", args[2]},
                           {"arguments", cymcp::parse_json(input)}};
            } else
                throw cymcp::Error("InvalidArguments",
                                   "Remote tools support list, describe NAME and call NAME JSON");
            auto token = env("CYSYNCK_API_TOKEN");
            if (token.empty())
                token = core.api_token();
            while (endpoint.ends_with('/'))
                endpoint.pop_back();
            cymcp::HttpRequest http;
            http.method = "POST";
            http.url = endpoint + "/v1";
            http.body = request.dump();
            http.headers = {{"Authorization", "Bearer " + token},
                            {"Content-Type", "application/json"}};
            auto response = cymcp::HttpClient{}.perform(http);
            if (response.status == 401 || response.status == 403)
                throw cymcp::Error(
                    "AccessDenied",
                    "The running instance rejected authentication or write permission");
            if (response.status != 200)
                throw cymcp::Error("RemoteError",
                                   "The running instance rejected this request (HTTP " +
                                       std::to_string(response.status) + ")");
            auto result = cymcp::parse_json(response.body, 16 * 1024 * 1024);
            if (result.contains("error"))
                throw cymcp::Error(result.at("error").value("code", "RemoteError"),
                                   "The running instance rejected this operation");
            if (args[1] == "list" || args[1] == "describe") {
                result = result.at("operations");
                if (args[1] == "describe") {
                    auto found = std::find_if(result.begin(), result.end(), [&](const Json &item) {
                        return item.at("name") == args[2];
                    });
                    if (found == result.end())
                        throw cymcp::Error("UnknownOperation", "Unknown operation");
                    result = *found;
                }
            }
            std::cout << result.dump(2) << '\n';
            return result_exit_code(result);
        }
        Host host(core, Options::load(core.state()));
        host.allow_writes(writes);
        if (args[0] == "tools") {
            if (args.size() >= 3 && args[1] == "call" &&
                args[2] == "cysynck.project.snapshot-inventory")
                throw cymcp::Error("InvalidArguments",
                                   "This retained export requires a running host: start 'cysynck "
                                   "mcp --port PORT' and call it with --endpoint URL; use tools "
                                   "read-resource URI --endpoint URL to recover its contents");
            auto result = host.command({args.begin() + 1, args.end()});
            std::cout << result.dump(2) << '\n';
            return result_exit_code(result);
        }
        if (args.size() == 2 && args[1] == "--stdio") {
            host.stdio(full);
            return 0;
        }
        if (args.size() == 3 && args[1] == "--port") {
            auto port = host.listen(std::stoi(args[2]), full);
            std::cerr << "CySynck MCP: http://127.0.0.1:" << port
                      << "/mcp; token file: " << path_string(core.state() / "api-token") << '\n';
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            while (!interrupted)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return 0;
        }
        if (args.size() == 1 || (args.size() == 2 && (args[1] == "--help" || args[1] == "help"))) {
            std::cout << host.command({"help"}).dump(2) << '\n';
            return 0;
        }
        throw cymcp::Error("InvalidArguments", "Use cysynck tools help");
    } catch (const cymcp::Error &error) {
        std::cerr << Json{{"error",
                           {{"code", error.code},
                            {"message", cymcp::redact_error_message(error.what())}}}}
                         .dump()
                  << '\n';
        return error.code == "PermissionDenied" || error.code == "AccessDenied" ? 3
               : error.code == "InvalidArguments"                               ? 2
                                                                                : 1;
    } catch (const std::exception &error) {
        std::cerr << Json{{"error",
                           {{"code", "OperationFailed"},
                            {"message", cymcp::redact_error_message(error.what())}}}}
                         .dump()
                  << '\n';
        return 1;
    }
}
} // namespace cysynck::assistant
