#include <cysynck/core.hpp>
#include "engine_options.hpp"
#include <httplib.h>
#include <tinyxml2.h>
#include <regex>
#include <thread>
#include <fstream>

namespace cysynck {
static void validate_url(const std::string &url) {
    std::smatch match;
    if (!std::regex_match(url, match,
                          std::regex("http://(127\\.0\\.0\\.1|localhost):([0-9]{1,5})")) ||
        std::stoi(match[2]) < 1 || std::stoi(match[2]) > 65535)
        throw std::runtime_error("Use a loopback HTTP Syncthing address, or an SSH tunnel");
}
Json Core::engine_config() const {
    auto result =
        registry_.at("settings")
            .value("engine", Json{{"kind", "external"},
                                  {"url", env("CYSYNCK_SYNCTHING_URL", "http://127.0.0.1:8384")},
                                  {"auto_start", false}});
    result["key_configured"] =
        fs::exists(state_ / "engine/api-key") ||
        (!registry_.at("settings").contains("engine") && !env("CYSYNCK_SYNCTHING_API_KEY").empty());
    return result;
}
Json Core::engine_request(const std::string &method, const std::string &path, const Json &data,
                          int timeout) {
    auto config = engine_config();
    auto url = config.at("url").get<std::string>();
    validate_url(url);
    auto secretPath = state_ / "engine/api-key";
    auto secret =
        fs::exists(secretPath) ? read_file(secretPath, 4096) : env("CYSYNCK_SYNCTHING_API_KEY");
    if (secret.empty())
        throw std::runtime_error("Configure a Syncthing connection in the Engine view");
    httplib::Client client(url);
    client.set_connection_timeout(std::min(timeout, 3));
    client.set_read_timeout(timeout);
    client.set_write_timeout(timeout);
    client.set_follow_location(false);
    httplib::Headers headers{{"X-API-Key", secret}};
    auto endpoint = "/rest/" + path;
    httplib::Result response;
    if (method == "GET")
        response = client.Get(endpoint, headers);
    else if (method == "PATCH")
        response = client.Patch(endpoint, headers, data.dump(), "application/json");
    else if (method == "PUT")
        response = client.Put(endpoint, headers, data.dump(), "application/json");
    else if (method == "DELETE")
        response = client.Delete(endpoint, headers);
    else
        response = client.Post(endpoint, headers, data.dump(), "application/json");
    if (!response)
        throw std::runtime_error("Syncthing is unreachable");
    if (response->status < 200 || response->status >= 300)
        throw std::runtime_error("Syncthing API returned HTTP " + std::to_string(response->status));
    return response->body.empty() ? Json::object() : Json::parse(response->body);
}
static tinyxml2::XMLElement *element(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent,
                                     const char *name) {
    auto child = parent->FirstChildElement(name);
    if (!child) {
        child = doc.NewElement(name);
        parent->InsertEndChild(child);
    }
    return child;
}
static void xml_text(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, const char *name,
                     const std::string &value) {
    element(doc, parent, name)->SetText(value.c_str());
}
Json Core::engine_action(const std::string &action, const Json &data) {
    auto directory = state_ / "engine";
    fs::create_directories(directory);
    if (action == "configure" || action == "connect") {
        if (engine_child_ && engine_child_->running())
            throw std::runtime_error("Stop the owned engine before changing its configuration");
        engine_child_.reset();
        engine_lease_.reset();
        DiskLock lease(directory, "owner.lock", false);
        Json config;
        if (action == "configure") {
            auto executable = fs::canonical(fs::u8path(data.at("executable").get<std::string>()));
            if (!fs::is_regular_file(executable))
                throw std::runtime_error("Select the Syncthing executable");
            int port = data.value("port", 8385), listen = data.value("listen_port", 22001);
            if (port < 1 || port > 65535 || listen < 1 || listen > 65535 || port == listen)
                throw std::runtime_error("Choose different valid API and synchronization ports");
            auto result = process({path_string(executable), "--version"}, {}, {}, 10);
            if (result.code || result.out.find("syncthing v") == std::string::npos)
                throw std::runtime_error(
                    "The selected executable did not identify itself as Syncthing");
            auto automatic = data.value("auto_start", false),
                 loopback = data.value("loopback_only", false);
            config = {{"kind", "managed"},
                      {"url", "http://127.0.0.1:" + std::to_string(port)},
                      {"executable", path_string(executable)},
                      {"port", port},
                      {"listen_port", listen},
                      {"auto_start", automatic},
                      {"loopback_only", loopback}};
            if (!fs::exists(directory / "api-key") ||
                registry_["settings"]
                        .value("engine", Json::object())
                        .value("kind", std::string{}) != "managed")
                atomic_write(directory / "api-key", random_id() + random_id(), true);
        } else {
            auto url = data.at("url").get<std::string>();
            validate_url(url);
            auto secret = data.value("api_key", std::string{});
            if (secret.empty() || secret.size() > 4096 ||
                secret.find_first_of("\r\n") != std::string::npos)
                throw std::runtime_error("Enter the existing instance API key");
            atomic_write(directory / "api-key", secret, true);
            config = {{"kind", "external"}, {"url", url}, {"auto_start", false}};
        }
        registry_["settings"]["engine"] = config;
        engine_requested_ = false;
        engine_suppressed_ = false;
        engine_error_.clear();
        event("", "engine", "Syncthing connection settings saved");
        return engine_config();
    }
    auto config = engine_config();
    auto status = [&] {
        Json output = {
            {"config", config},       {"owned", engine_child_ && engine_child_->running()},
            {"running", false},       {"requested_running", engine_requested_},
            {"error", engine_error_}, {"pid", engine_child_ ? engine_child_->pid() : 0}};
        if (config.at("key_configured").get<bool>())
            try {
                auto state = engine_request("GET", "system/status", Json::object(), 1);
                output["running"] = true;
                output["device_id"] = state.at("myID");
                output["uptime"] = state.value("uptime", 0);
                output["version"] = engine_request("GET", "system/version", Json::object(), 1)
                                        .value("version", std::string{});
            } catch (const std::exception &e) {
                output["error"] = e.what();
            }
        return output;
    };
    if (action == "status")
        return status();
    if (action == "stop") {
        if (!engine_child_)
            throw std::runtime_error("This controller does not own a Syncthing child; external "
                                     "instances are never stopped");
        engine_requested_ = false;
        engine_suppressed_ = true;
        if (engine_child_->running()) {
            try {
                engine_request("POST", "system/shutdown", Json::object(), 2);
            } catch (...) {
            }
            if (!engine_child_->wait(5000))
                engine_child_->terminate();
        }
        engine_child_.reset();
        engine_lease_.reset();
        engine_error_.clear();
        event("", "engine", "Owned Syncthing instance stopped");
        return {{"stopped", true}};
    }
    if (action == "start") {
        if (config.at("kind") != "managed")
            throw std::runtime_error("Choose a managed Syncthing executable before starting");
        if (engine_child_ && engine_child_->running()) {
            engine_suppressed_ = false;
            return status();
        }
        engine_child_.reset();
        engine_lease_.reset();
        try {
            engine_lease_ = std::make_unique<DiskLock>(directory, "owner.lock", false);
        } catch (const std::exception &) {
            auto result = status();
            if (result["running"].get<bool>()) {
                result["attached"] = true;
                return result;
            }
            throw std::runtime_error("Another CySynck controller owns this engine");
        }
        try {
            {
                httplib::Client probe("127.0.0.1", config.at("port").get<int>());
                probe.set_connection_timeout(1);
                probe.set_read_timeout(1);
                auto response = probe.Get("/rest/system/ping");
                if (response || (response.error() != httplib::Error::Connection &&
                                 response.error() != httplib::Error::ConnectionTimeout))
                    throw std::runtime_error("The selected Syncthing API port is already occupied");
            }
            auto home = directory / "syncthing";
            fs::create_directories(home);
            auto path = home / "config.xml";
            bool first = !fs::exists(path);
            auto executable = config.at("executable").get<std::string>();
            auto help = [&](const std::string &command) {
                auto result = process({executable, command, "--help"}, home, {}, 10);
                if (result.code)
                    throw std::runtime_error("Cannot inspect Syncthing " + command + " options");
                return result.out;
            };
            auto serve_help = help("serve");
            auto serve_probing =
                syncthing_option(serve_help, {"--no-port-probing", "--skip-port-probing"});
            auto log_file = syncthing_option(serve_help, {"--log-file", "--logfile"});
            if (first) {
                auto generate_probing = syncthing_option(
                    help("generate"), {"--no-port-probing", "--skip-port-probing"});
                auto generated =
                    process({executable, "generate", "--home", path_string(home), generate_probing},
                            home, {}, 30);
                if (generated.code)
                    throw std::runtime_error(
                        "Syncthing could not generate its isolated configuration");
            }
            tinyxml2::XMLDocument doc;
            if (doc.Parse(read_file(path, 8 * 1024 * 1024).c_str()) != tinyxml2::XML_SUCCESS)
                throw std::runtime_error("Cannot parse the isolated Syncthing configuration");
            auto root = doc.FirstChildElement("configuration");
            if (!root)
                throw std::runtime_error("Missing Syncthing configuration root");
            if (first) {
                while (auto folder = root->FirstChildElement("folder"))
                    root->DeleteChild(folder);
            }
            auto gui = element(doc, root, "gui");
            gui->SetAttribute("enabled", true);
            gui->SetAttribute("tls", false);
            xml_text(doc, gui, "address",
                     "127.0.0.1:" + std::to_string(config.at("port").get<int>()));
            xml_text(doc, gui, "apikey", read_file(directory / "api-key", 4096));
            auto options = element(doc, root, "options");
            while (auto listener = options->FirstChildElement("listenAddress"))
                options->DeleteChild(listener);
            xml_text(doc, options, "listenAddress",
                     std::string("tcp://") +
                         (config.at("loopback_only").get<bool>() ? "127.0.0.1:" : "0.0.0.0:") +
                         std::to_string(config.at("listen_port").get<int>()));
            xml_text(doc, options, "startBrowser", "false");
            xml_text(doc, options, "autoUpgradeIntervalH", "0");
            for (auto name :
                 {"globalAnnounceEnabled", "localAnnounceEnabled", "relaysEnabled", "natEnabled"})
                xml_text(doc, options, name,
                         config.at("loopback_only").get<bool>() ? "false" : "true");
            tinyxml2::XMLPrinter printer;
            doc.Print(&printer);
            atomic_write(path, printer.CStr(), true);
            engine_child_ = std::make_unique<ChildProcess>(
                std::vector<std::string>{executable, "serve", "--home", path_string(home),
                                         "--no-browser", "--no-upgrade", "--no-restart",
                                         serve_probing, log_file,
                                         path_string(directory / "syncthing.log"), "--log-max-size",
                                         "1048576", "--log-max-old-files", "2"},
                home, std::map<std::string, std::string>{{"STMONITORED", "1"}});
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (std::chrono::steady_clock::now() < deadline) {
                if (!engine_child_->running())
                    throw std::runtime_error(
                        "Syncthing exited during startup; inspect the engine log");
                try {
                    engine_request("GET", "system/status", Json::object(), 1);
                    engine_requested_ = true;
                    engine_suppressed_ = false;
                    engine_error_.clear();
                    event("", "engine", "Isolated Syncthing instance started");
                    return status();
                } catch (const std::exception &) {
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            throw std::runtime_error("Syncthing startup timed out; inspect the engine log");
        } catch (...) {
            engine_child_.reset();
            engine_lease_.reset();
            throw;
        }
    }
    if (action == "devices")
        return {{"devices", engine_request("GET", "config/devices")},
                {"connections", engine_request("GET", "system/connections")}};
    if (action == "add-device") {
        auto device = data.at("device_id").get<std::string>();
        if (!std::regex_match(device, std::regex("[A-Z2-7]{7}(-[A-Z2-7]{7}){7}")))
            throw std::runtime_error("Invalid Syncthing device ID");
        auto address = data.value("address", std::string("dynamic"));
        if (address != "dynamic" &&
            !std::regex_match(address, std::regex("(tcp|quic)://[a-zA-Z0-9.\\[\\]:-]+:[0-9]{1,5}")))
            throw std::runtime_error("Use a Syncthing tcp://host:port or quic://host:port address");
        for (auto &entry : engine_request("GET", "config/devices"))
            if (entry.at("deviceID") == device)
                throw std::runtime_error("Device is already configured; edit it in Syncthing Web");
        auto entry = engine_request("GET", "config/defaults/device");
        entry["deviceID"] = device;
        entry["name"] = data.value("name", std::string("CySynck peer"));
        entry["addresses"] = Json::array({address});
        entry["autoAcceptFolders"] = false;
        engine_request("POST", "config/devices", entry);
        return {{"device_id", device}, {"added", true}};
    }
    if (action == "share") {
        auto &p = project(data.at("project"));
        auto device = data.at("device_id").get<std::string>();
        if (!std::regex_match(device, std::regex("[A-Z2-7]{7}(-[A-Z2-7]{7}){7}")))
            throw std::runtime_error("Invalid Syncthing device ID");
        engine_request("GET", "config/devices/" + device);
        auto folderId = "cysynck-" + p.at("id").get<std::string>();
        auto folder = engine_request("GET", "config/folders/" + folderId);
        auto expected =
            p.at(p.at("mode") == "git-bundles" ? "exchange_path" : "root").get<std::string>();
        if (fs::weakly_canonical(fs::u8path(folder.at("path").get<std::string>())) !=
            fs::weakly_canonical(fs::u8path(expected)))
            throw std::runtime_error("Folder path does not match the project");
        auto devices = folder.at("devices");
        for (auto &item : devices)
            if (item.at("deviceID") == device)
                return {{"shared", true}};
        devices.push_back({{"deviceID", device}});
        engine_request("PATCH", "config/folders/" + folderId, {{"devices", devices}});
        event(p.at("id"), "syncthing", "Project shared with configured peer");
        return {{"shared", true}};
    }
    if (action == "log") {
        auto path = directory / "syncthing.log";
        if (!fs::exists(path))
            return {{"text", ""}};
        std::ifstream input(path, std::ios::binary);
        auto size = fs::file_size(path);
        if (size > 32000)
            input.seekg(static_cast<std::streamoff>(size - 32000));
        std::string text((std::istreambuf_iterator<char>(input)), {});
        return {{"text", text}};
    }
    throw std::runtime_error("Unknown engine action");
}
void Core::maintain_engine() {
    auto config = engine_config();
    if (config.at("kind") != "managed" || engine_suppressed_ ||
        (!config.value("auto_start", false) && !engine_requested_))
        return;
    if (engine_child_ && engine_child_->running())
        return;
    if (std::chrono::steady_clock::now() < engine_retry_)
        return;
    engine_retry_ = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    try {
        engine_action("start", Json::object());
    } catch (const std::exception &error) {
        engine_error_ = error.what();
        event("", "engine-error", engine_error_);
    }
}
} // namespace cysynck
