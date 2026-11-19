#include "assistant.hpp"
#include "assistant_action_schemas.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace cysynck::assistant {
struct DesktopBridge::Request {
    std::string name;
    Json arguments;
    std::promise<Json> promise;
    bool started = false, cancelled = false;
};
Json DesktopBridge::invoke(const std::string &name, const Json &arguments,
                           const cymcp::CallContext &context) {
    context.cancellation.check();
    auto request = std::make_shared<Request>();
    request->name = name;
    request->arguments = arguments;
    auto future = request->promise.get_future();
    {
        std::lock_guard lock(mutex_);
        if (closed_)
            throw cymcp::Error("InstanceClosed", "The desktop instance is closing");
        if (queue_.size() >= 32)
            throw cymcp::Error("Busy", "The desktop action queue is full");
        queue_.push_back(request);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
        std::lock_guard lock(mutex_);
        // Once execution starts, return its receipt even if cancellation arrives.
        if (request->started)
            continue;
        try {
            context.cancellation.check();
            if (std::chrono::steady_clock::now() >= deadline)
                throw cymcp::Error("InstanceBusy",
                                   "The desktop did not accept this action; nothing was applied");
        } catch (...) {
            request->cancelled = true;
            queue_.erase(std::remove(queue_.begin(), queue_.end(), request), queue_.end());
            throw;
        }
    }
    return future.get();
}
void DesktopBridge::drain(const Handler &handler) {
    for (int n = 0; n < 32; ++n) {
        std::shared_ptr<Request> request;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty())
                return;
            request = queue_.front();
            queue_.pop_front();
            request->started = !request->cancelled;
        }
        if (!request->started)
            continue;
        try {
            request->promise.set_value(handler(request->name, request->arguments));
        } catch (...) {
            request->promise.set_exception(std::current_exception());
        }
    }
}
void DesktopBridge::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    for (const auto &request : queue_) {
        request->cancelled = true;
        request->promise.set_exception(std::make_exception_ptr(cymcp::Error(
            "InstanceClosed", "The desktop closed before this action; nothing was applied")));
    }
    queue_.clear();
}

Json Host::receiver_action(const std::string &action) {
    std::lock_guard lock(receiver_mutex_);
    if (action == "stop") {
        if (receiver_)
            receiver_->stop();
        if (receiver_thread_.joinable())
            receiver_thread_.join();
        receiver_.reset();
        receiver_address_.clear();
        receiver_port_ = 0;
    } else if (action == "start" && !receiver_) {
        auto profile = core_.request("GET", "/api/network/status").at("profile");
        if (profile.is_null())
            throw cymcp::Error("NotConfigured",
                               "Configure WireGuard before starting the direct receiver");
        auto receiver = std::make_unique<HttpServer>(core_, true);
        auto address = profile.at("address").get<std::string>();
        auto port = profile.at("mesh_port").get<int>();
        receiver->bind(address, port);
        receiver_ = std::move(receiver);
        try {
            receiver_thread_ = std::thread([this] { receiver_->listen(); });
        } catch (...) {
            receiver_->stop();
            receiver_.reset();
            throw;
        }
        receiver_address_ = address;
        receiver_port_ = port;
    }
    return {{"running", bool(receiver_)},
            {"address", receiver_address_},
            {"port", receiver_port_},
            {"direct_only", true}};
}

void Host::register_extended_operations() {
    struct Route {
        const char *name, *description, *method, *path;
        bool read, persistent = false, project = false;
    };
    for (const auto &route :
         {Route{"engine.configure",
                "Configure a managed Syncthing executable and isolated ports; stop the owned "
                "engine first.",
                "POST", "/api/engine/configure", false},
          {"engine.connect",
           "Connect to an existing Syncthing instance using an API key from api_key_env.", "POST",
           "/api/engine/connect", false},
          {"engine.start",
           "Start the owned engine in this persistent instance. It is stopped when its owner "
           "exits.",
           "POST", "/api/engine/start", false, true},
          {"engine.stop",
           "Stop only the Syncthing process owned by this instance; external instances are "
           "protected.",
           "POST", "/api/engine/stop", false, true},
          {"engine.add-device",
           "Add a configured Syncthing device; folder acceptance remains explicit.", "POST",
           "/api/engine/add-device", false},
          {"engine.share", "Share a registered project with an existing Syncthing device.", "POST",
           "/api/engine/share", false},
          {"network.configure",
           "Generate a local WireGuard identity and save its public network settings. Requires "
           "installed WireGuard tools.",
           "POST", "/api/network/configure", false},
          {"network.preview", "Preview WireGuard configuration with the private key omitted.",
           "POST", "/api/network/preview", true},
          {"network.write-config",
           "Write the private WireGuard configuration to the protected local profile; returns only "
           "its path.",
           "POST", "/api/network/write-config", false},
          {"network.up",
           "Activate the owned WireGuard tunnel using the native platform checks and privileges.",
           "POST", "/api/network/up", false},
          {"network.down",
           "Deactivate only the owned WireGuard tunnel and release its router mapping.", "POST",
           "/api/network/down", false},
          {"network.apparmor",
           "Export Linux AppArmor guidance; does not install or change policies.", "POST",
           "/api/network/apparmor", true},
          {"network.router",
           "Enable, renew or disable a native UDP router lease for an owned live WireGuard "
           "listener.",
           "POST", "/api/network/router", false},
          {"network.automatic",
           "Enable or disable signed endpoint refresh; a running maintenance controller is "
           "required.",
           "POST", "/api/network/automatic", false},
          {"network.refresh",
           "Refresh signed pinned endpoints and update the owned tunnel through native "
           "verification.",
           "POST", "/api/network/refresh", false},
          {"network.publish",
           "Publish this device's public pairing profile in the project's exchange directory.",
           "POST", "/api/network/publish", false},
          {"network.discover",
           "Find public peer profiles in an existing exchange; discovery does not grant trust.",
           "POST", "/api/network/discover", true},
          {"network.trust",
           "Pin a public peer profile. expected_key and any signing fingerprint must have been "
           "independently confirmed by the user; never infer consent from discovery.",
           "POST", "/api/network/trust", false},
          {"network.revoke", "Revoke a peer for one project; preserves endpoint replay protection.",
           "POST", "/api/network/revoke", false},
          {"chat.send",
           "Send the user-requested direct chat message to a paired peer through the active VPN. "
           "This communicates with another person; require an explicit user request.",
           "POST", "/api/network/chat", false},
          {"transfers.send",
           "Send a user-selected project file to a paired peer using resumable transfer. Requires "
           "an explicit user request to send this file.",
           "POST", "/api/network/send-file", false},
          {"transfers.resume",
           "Resume one frozen outgoing transfer through the paired peer's active VPN receiver.",
           "POST", "/api/network/transfer-resume", false},
          {"transfers.cancel",
           "Remove one local transfer journal and its pending blocks; completed received files are "
           "preserved.",
           "POST", "/api/network/transfer-cancel", false},
          {"transfers.configure",
           "Save direct transfer size and storage quotas; current usage must fit.", "POST",
           "/api/network/transfer-configure", false},
          {"cyrevision.identity",
           "Read the public CyRevision identity and fingerprint; never returns its signing key.",
           "GET", "/api/cyrevision/identity", true},
          {"cyrevision.identity-create",
           "Create the local CyRevision signing identity if absent; returns only the public "
           "identity.",
           "POST", "/api/cyrevision/identity", false},
          {"project.cyrevision-configure",
           "Associate this Git exchange project with a CyRevision GUID and signature format.",
           "POST", "/api/projects/{project}/cyrevision-configure", false, false, true},
          {"project.cyrevision-trust",
           "Pin a CyRevision publishing identity using a fingerprint independently confirmed by "
           "the user.",
           "POST", "/api/projects/{project}/cyrevision-trust", false, false, true},
          {"project.cyrevision-revoke",
           "Revoke a CyRevision publisher while retaining existing review references.", "POST",
           "/api/projects/{project}/cyrevision-revoke", false, false, true},
          {"project.cyrevision-export",
           "Publish a signed CyRevision transaction through the configured exchange.", "POST",
           "/api/projects/{project}/cyrevision-export", false, false, true},
          {"project.cyrevision-import",
           "Verify and import a signed CyRevision transaction into review references; does not "
           "merge the working tree.",
           "POST", "/api/projects/{project}/cyrevision-import", false, false, true},
          {"project.cyrevision-recover",
           "Preview interrupted CyRevision import recovery. Applying requires apply=true and its "
           "exact plan token.",
           "POST", "/api/projects/{project}/cyrevision-recover", false, false, true},
          {"service.plan",
           "Prepare a Linux user service or macOS launch agent; does not install it.", "POST",
           "/api/service/plan", true},
          {"service.export",
           "Export the reviewed native service files to a new directory with the exact preview "
           "plan; does not install them.",
           "POST", "/api/service/export", false},
          {"help.location",
           "Resolve the bundled offline help page, section and language to a file URL.", "POST",
           "/api/help/location", true},
          {"maintenance.tick",
           "Run the native maintenance scheduler once, including configured backups and "
           "owned-engine supervision.",
           "POST", "/api/maintenance/tick", false},
          {"settings.get", "Read saved application settings with private credentials filtered.",
           "GET", "/api/settings", true},
          {"settings.set",
           "Save validated desktop preferences; applies live in a desktop host, otherwise on the "
           "next launch.",
           "PUT", "/api/settings", false}}) {
        std::string name = "cysynck." + std::string(route.name);
        cymcp::Operation operation;
        operation.name = name;
        operation.description = route.description;
        Json properties = {{"parameters", *schemas::extended_parameters(name)}};
        if (route.project)
            properties["project_id"] = {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}};
        operation.input_schema = {
            {"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
        if (route.project)
            operation.input_schema["required"] = {"project_id"};
        // Required native parameters must not disappear by omitting the wrapper.
        if (!properties["parameters"].at("required").empty()) {
            if (!operation.input_schema.contains("required"))
                operation.input_schema["required"] = Json::array();
            operation.input_schema["required"].push_back("parameters");
        }
        operation.output_schema = {{"type", "object"},
                                   {"properties", {{"result", Json::object()}}},
                                   {"required", {"result"}}};
        operation.read_only = route.read;
        operation.handler = [this, route, name](const Json &arguments,
                                                const cymcp::CallContext &context) {
            context.cancellation.check();
            if (route.persistent && !persistent_.load())
                throw cymcp::Error("InvalidArguments",
                                   "This operation requires a persistent instance: use mcp "
                                   "--stdio, mcp --port PORT or tools call --endpoint URL");
            auto parameters = arguments.value("parameters", Json::object());
            auto path = std::string(route.path);
            if (route.project) {
                auto id = arguments.at("project_id").get<std::string>();
                if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                        return std::isalnum(c) || c == '-' || c == '_';
                    }))
                    throw cymcp::Error("InvalidArguments", "Invalid project ID");
                path.replace(path.find("{project}"), 9, id);
            }
            if (name == "cysynck.engine.connect") {
                auto variable = parameters.at("api_key_env").get<std::string>();
                if (!std::regex_match(variable, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    throw cymcp::Error("InvalidArguments",
                                       "Choose an environment variable name for the API key");
                auto secret = env(variable.c_str());
                if (secret.empty())
                    throw cymcp::Error("InvalidArguments",
                                       "The API key environment variable is empty");
                parameters.erase("api_key_env");
                parameters["api_key"] = secret;
            }
            Json result;
            // UI state is modified exclusively on the desktop's own thread.
            if (name == "cysynck.settings.set" && desktop_)
                result = desktop_->invoke("cysynck.ui.configure", parameters, context);
            else
                result = core_.request(route.method, path, parameters);
            if (!route.read)
                changed();
            // Core's new routes return public identities or paths, never raw
            // secrets. Reuse the shared recursive redactor for settings output.
            return sanitize_result(std::move(result));
        };
        registry_->add(std::move(operation));
    }
    for (auto name : {"ui.status", "ui.configure", "ui.help", "startup.status", "startup.enable",
                      "startup.disable", "receiver.status", "receiver.start", "receiver.stop",
                      "plugins.load"}) {
        std::string full = "cysynck." + std::string(name);
        cymcp::Operation operation;
        operation.name = full;
        operation.description =
            full +
            ": invoke the actual running application instance. UI and startup actions require a "
            "desktop endpoint; native plugin loading executes reviewed code and requires its "
            "expected_sha256. Receiver actions use the configured VPN interface.";
        operation.input_schema = {
            {"type", "object"},
            {"properties", {{"parameters", *schemas::extended_parameters(full)}}},
            {"additionalProperties", false}};
        if (!schemas::extended_parameters(full)->at("required").empty())
            operation.input_schema["required"] = {"parameters"};
        operation.output_schema = {{"type", "object"},
                                   {"properties", {{"result", Json::object()}}},
                                   {"required", {"result"}}};
        operation.read_only = full.ends_with(".status");
        operation.handler = [this, full](const Json &arguments, const cymcp::CallContext &context) {
            context.cancellation.check();
            auto parameters = arguments.value("parameters", Json::object());
            Json result;
            if (full == "cysynck.plugins.load") {
                if (!persistent_.load())
                    throw cymcp::Error(
                        "InvalidArguments",
                        "Native plugins require a persistent host; use --endpoint or mcp");
                auto path = fs::u8path(parameters.at("path").get<std::string>());
                if (sha256_file(path) != parameters.at("expected_sha256").get<std::string>())
                    throw cymcp::Error("InvalidArguments",
                                       "Native plugin hash differs from the reviewed library");
                core_.load_plugin(path);
                result = core_.request("GET", "/api/plugins");
            } else if (full.starts_with("cysynck.receiver.") && !desktop_) {
                if (!full.ends_with(".status") && !persistent_.load())
                    throw cymcp::Error("InvalidArguments",
                                       "Direct receiver control requires a persistent host; use "
                                       "--endpoint or mcp");
                result = receiver_action(full.substr(std::string("cysynck.receiver.").size()));
            } else {
                if (!desktop_)
                    throw cymcp::Error("CapabilityUnavailable",
                                       "This action requires the running desktop's MCP endpoint");
                result = desktop_->invoke(full, parameters, context);
            }
            changed();
            return sanitize_result(std::move(result));
        };
        registry_->add(std::move(operation));
    }
}
} // namespace cysynck::assistant
