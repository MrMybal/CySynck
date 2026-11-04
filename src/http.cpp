#include <cysynck/core.hpp>
#include <httplib.h>
#include <regex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace cysynck {
static bool secure_equal(const std::string &a, const std::string &b) {
    size_t diff = a.size() ^ b.size();
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}
Json Core::sync_action(Json &p, const std::string &action, const Json &data) {
    auto mode = p.at("mode").get<std::string>();
    auto folderId = "cysynck-" + p.at("id").get<std::string>();
    auto expected = p.at(mode == "git-bundles" ? "exchange_path" : "root").get<std::string>();
    auto usesEngine = mode == "sendreceive" || mode == "sendonly" || mode == "receiveonly" ||
                      mode == "git-bundles";
    auto check = [&] {
        auto folder = engine_request("GET", "config/folders/" + folderId);
        if (fs::weakly_canonical(fs::u8path(folder.at("path").get<std::string>())) !=
            fs::weakly_canonical(fs::u8path(expected)))
            throw std::runtime_error("Syncthing folder path does not match this project");
        return folder;
    };
    if (action == "register") {
        if (!usesEngine)
            throw std::runtime_error("This mode does not use Syncthing");
        if (mode != "git-bundles" &&
            fs::exists(fs::u8path(p.at("root").get<std::string>()) / ".git"))
            throw std::runtime_error("Use Git exchange mode; an active .git must remain local");
        bool existing = false;
        for (auto &folder : engine_request("GET", "config/folders"))
            if (folder.at("id") == folderId) {
                check();
                existing = true;
                break;
            }
        if (!existing) {
            auto folder = engine_request("GET", "config/defaults/folder");
            folder["id"] = folderId;
            folder["label"] = p.at("name");
            folder["path"] = expected;
            folder["type"] = mode == "git-bundles" ? "sendreceive" : mode;
            folder["paused"] = p.at("paused");
            folder["devices"] =
                Json::array({{{"deviceID", engine_request("GET", "system/status").at("myID")}}});
            folder["versioning"] = {
                {"type", "staggered"},
                {"params", {{"maxAge", "31536000"}, {"cleanInterval", "3600"}}}};
            // Create paused, install the .git exclusion, then apply the requested
            // state.
            folder["paused"] = true;
            if (mode == "git-bundles")
                fs::create_directories(fs::u8path(expected));
            engine_request("POST", "config/folders", folder);
        } else {
            engine_request("PATCH", "config/folders/" + folderId, {{"paused", true}});
        }
        auto ignores =
            engine_request("GET", "db/ignores?folder=" + folderId).value("ignore", Json::array());
        if (ignores.is_null())
            ignores = Json::array();
        if (!ignores.is_array())
            throw std::runtime_error(
                "Syncthing returned invalid ignore rules; folder remains paused");
        if (std::find(ignores.begin(), ignores.end(), Json("(?i).git")) == ignores.end())
            ignores.insert(ignores.begin(), "(?i).git");
        Json private_ignores = Json::array({"(?i).cysynck-recovery"});
        for (const auto &line : ignores)
            if (line != "(?i).cysynck-recovery")
                private_ignores.push_back(line);
        ignores = private_ignores;
        engine_request("POST", "db/ignores?folder=" + folderId, {{"ignore", ignores}});
        engine_request("PATCH", "config/folders/" + folderId, {{"paused", p.at("paused")}});
        event(p.at("id"), "syncthing",
              "Project folder registered; select peers in the Engine view");
        return {{"folder_id", folderId},
                {"existing", existing},
                {"next", "Add and share with authorized peers on both devices in the Engine view"}};
    }
    if (action == "status")
        return usesEngine ? engine_request("GET", "db/status?folder=" + folderId)
                          : Json{{"state", "local"}};
    if (action == "pause") {
        bool paused = data.at("paused").get<bool>();
        if (usesEngine) {
            check();
            engine_request("PATCH", "config/folders/" + folderId, {{"paused", paused}});
        }
        p["paused"] = paused;
        event(p.at("id"), "project", paused ? "Project paused" : "Project resumed");
        return p;
    }
    if (action != "sync")
        throw std::runtime_error("Unknown project action");
    if (p.at("paused").get<bool>())
        throw std::runtime_error("Project is paused");
    if (p.at("backup_before_sync").get<bool>())
        backup(p, "before-sync");
    hook("before_sync", p);
    Json result;
    if (mode == "backup")
        result = p.at("backup_before_sync").get<bool>() ? Json{{"state", "backed-up"}}
                                                        : backup(p, "manual");
    else if (usesEngine) {
        check();
        if (mode == "git-bundles")
            git_action(p, "export", Json::object());
        result = engine_request("POST", "db/scan?folder=" + folderId);
    } else
        result = request_plugin_mode(p);
    event(p.at("id"), "sync", "Synchronization action completed");
    return result;
}

static fs::path assets() {
    if (!env("CYSYNCK_WEB_ROOT").empty())
        return fs::u8path(env("CYSYNCK_WEB_ROOT"));
    fs::path executable;
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(count);
    executable = buffer;
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    _NSGetExecutablePath(buffer.data(), &size);
    executable = fs::canonical(buffer.c_str());
#else
    executable = fs::read_symlink("/proc/self/exe");
#endif
    for (auto path :
         {executable.parent_path() / "web", executable.parent_path() / "../Resources/web",
          executable.parent_path() / "../share/cysynck/web", fs::current_path() / "assets/web"})
        if (fs::is_directory(path))
            return path;
    throw std::runtime_error("Web assets missing; set CYSYNCK_WEB_ROOT");
}
struct HttpServer::Impl {
    Core &core;
    bool direct;
    httplib::Server server;
    std::string token;
    Impl(Core &value, bool mesh) : core(value), direct(mesh) {
        token = direct ? env("CYSYNCK_MESH_TOKEN") : core.api_token();
        if (token.size() < 24)
            throw std::runtime_error("A direct receiver requires CYSYNCK_MESH_TOKEN "
                                     "(at least 24 characters)");
        server.set_payload_max_length(direct ? 100 * 1024 * 1024 : 1024 * 1024);
        server.set_read_timeout(60);
        server.set_write_timeout(60);
        server.set_pre_routing_handler([this](const httplib::Request &request,
                                              httplib::Response &response) {
            response.set_header("Cache-Control", "no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            response.set_header("Content-Security-Policy",
                                "default-src 'self'; frame-ancestors 'none'; "
                                "object-src 'none'; base-uri 'none'");
            if ((direct || request.path.rfind("/api/", 0) == 0) &&
                !secure_equal(request.get_header_value("Authorization"), "Bearer " + token)) {
                response.status = 401;
                response.set_content("{\"error\":\"Authentication required\"}", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            auto origin = request.get_header_value("Origin");
            if (!origin.empty() && origin != "http://" + request.get_header_value("Host") &&
                origin != "https://" + request.get_header_value("Host")) {
                response.status = 403;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        auto handler = [this](const httplib::Request &req, httplib::Response &res) {
            try {
                Json result;
                if (direct) {
                    bool transfer = req.path.rfind("/transfer/", 0) == 0;
                    bool chunk = req.path == "/transfer/chunk";
                    Json data = req.path == "/message" || (transfer && !chunk)
                                    ? Json::parse(req.body)
                                    : Json::object();
                    data["peer"] = req.get_header_value("X-CySynck-Peer");
                    if (transfer)
                        data["transfer_action"] = req.path.substr(10);
                    if (chunk) {
                        data["project"] = req.get_header_value("X-CySynck-Project");
                        data["id"] = req.get_header_value("X-CySynck-Id");
                        data["chunk_sha256"] = req.get_header_value("X-CySynck-Chunk-SHA256");
                        data["index"] = Json::parse(req.get_header_value("X-CySynck-Chunk"));
                    }
                    if (req.path == "/file") {
                        data["file"] = true;
                        data["project"] = req.get_header_value("X-CySynck-Project");
                        data["id"] = req.get_header_value("X-CySynck-Id");
                        data["sha256"] = req.get_header_value("X-CySynck-SHA256");
                        data["name"] = httplib::detail::decode_url(
                            req.get_header_value("X-CySynck-Name"), false);
                    }
                    result = core.direct_receive(data, req.remote_addr,
                                                 req.path == "/file" || chunk ? req.body
                                                                              : std::string{});
                } else {
                    if (req.path == "/api/network/up" || req.path == "/api/network/down" ||
                        req.path == "/api/engine/configure" || req.path == "/api/engine/connect") {
                        res.status = 403;
                        res.set_content("{\"error\":\"Use the local desktop or "
                                        "CLI for tunnel activation and engine configuration\"}",
                                        "application/json");
                        return;
                    }
                    if (req.path == "/api/cli" && req.method == "POST") {
                        auto arguments =
                            Json::parse(req.body).at("args").get<std::vector<std::string>>();
                        if (arguments.size() > 1 && arguments[0] == "desktop" &&
                            arguments[1] == "startup")
                            throw std::runtime_error(
                                "Login startup settings require the local desktop or CLI");
                        if (arguments.size() > 1 && arguments[0] == "vpn" &&
                            (arguments[1] == "up" || arguments[1] == "down"))
                            throw std::runtime_error("Tunnel changes require the local CLI");
                        if (arguments.size() > 1 && arguments[0] == "engine" &&
                            (arguments[1] == "configure" || arguments[1] == "connect" ||
                             arguments[1] == "run"))
                            throw std::runtime_error(
                                "Engine executable and credentials must be configured locally");
                        result = cli(core, arguments);
                    } else
                        result =
                            core.request(req.method, req.path,
                                         req.body.empty() ? Json::object() : Json::parse(req.body));
                }
                res.set_content(result.dump(), "application/json; charset=utf-8");
            } catch (const std::exception &e) {
                res.status = 400;
                res.set_content(
                    Json{{"error", e.what()}}.dump(-1, ' ', false, Json::error_handler_t::replace),
                    "application/json");
            }
        };
        if (direct) {
            server.Post("/transfer/begin", handler);
            server.Post("/transfer/chunk", handler);
            server.Post("/transfer/finish", handler);
            server.Post("/message", handler);
            server.Post("/file", handler);
        } else {
            server.Get("/api/.*", handler);
            server.Post("/api/.*", handler);
            server.Put("/api/.*", handler);
            server.Get("/|/app.js|/i18n.js|/style.css", [](const httplib::Request &req,
                                                           httplib::Response &res) {
                try {
                    auto file = req.path == "/" ? "index.html" : req.path.substr(1);
                    res.set_content(read_file(assets() / file, 1024 * 1024),
                                    file == "index.html"    ? "text/html; charset=utf-8"
                                    : file.ends_with(".js") ? "text/javascript; charset=utf-8"
                                                            : "text/css; charset=utf-8");
                } catch (const std::exception &e) {
                    res.status = 404;
                    res.set_content(e.what(), "text/plain");
                }
            });
        }
    }
};
HttpServer::HttpServer(Core &core, bool direct) : impl_(std::make_unique<Impl>(core, direct)) {}
HttpServer::~HttpServer() { stop(); }
int HttpServer::bind(const std::string &host, int port) {
    if (impl_->direct) {
        // Binding only needs the configured identity, not a WireGuard status process or config
        // render.
        auto &core = impl_->core;
        std::lock_guard lock(core.mutex_);
        DiskLock disk(core.state_);
        core.load();
        auto profile = core.registry_.value("network", Json::object()).value("profile", Json());
        if (profile.is_null() || host != profile.at("address").get<std::string>())
            throw std::runtime_error("Direct receiver must bind to the configured VPN address");
    }
    if (port == 0)
        return impl_->server.bind_to_any_port(host);
    if (!impl_->server.bind_to_port(host, port))
        throw std::runtime_error("Cannot bind listener; check the address, tunnel and port");
    return port;
}
void HttpServer::listen() { impl_->server.listen_after_bind(); }
void HttpServer::stop() {
    if (impl_)
        impl_->server.stop();
}
Json remote_cli(const std::string &url, const std::vector<std::string> &arguments) {
    if (!std::regex_match(url, std::regex("http://(127\\.0\\.0\\.1|localhost):[0-9]{1,5}")))
        throw std::runtime_error("Use a loopback HTTP API or an SSH tunnel");
    httplib::Client client(url);
    client.set_connection_timeout(5);
    client.set_read_timeout(180);
    client.set_follow_location(false);
    auto response =
        client.Post("/api/cli", {{"Authorization", "Bearer " + env("CYSYNCK_API_TOKEN")}},
                    Json{{"args", arguments}}.dump(), "application/json");
    if (!response)
        throw std::runtime_error("CySynck server unreachable");
    auto result = Json::parse(response->body);
    if (response->status != 200)
        throw std::runtime_error(result.value("error", "Request failed"));
    return result;
}
} // namespace cysynck
