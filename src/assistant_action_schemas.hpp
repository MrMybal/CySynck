#pragma once
#include <cysynck/core.hpp>
#include <map>
#include <optional>
#include <sstream>

namespace cysynck::assistant::schemas {
inline std::optional<Json> extended_parameters(const std::string &operation) {
    static const std::map<std::string, std::pair<std::string, std::string>> fields = {
        {"cysynck.engine.configure",
         {"executable port listen_port auto_start loopback_only", "executable"}},
        {"cysynck.engine.connect", {"url api_key_env", "url api_key_env"}},
        {"cysynck.engine.start", {"", ""}},
        {"cysynck.engine.stop", {"", ""}},
        {"cysynck.engine.add-device", {"device_id name address", "device_id"}},
        {"cysynck.engine.share", {"project device_id", "project device_id"}},
        {"cysynck.network.configure",
         {"name address endpoint listen_port mesh_port device_id", "name address"}},
        {"cysynck.network.preview", {"", ""}},
        {"cysynck.network.write-config", {"", ""}},
        {"cysynck.network.up", {"", ""}},
        {"cysynck.network.down", {"", ""}},
        {"cysynck.network.apparmor", {"", ""}},
        {"cysynck.network.router",
         {"action gateway protocol lifetime_seconds external_port", "action"}},
        {"cysynck.network.automatic", {"enabled", "enabled"}},
        {"cysynck.network.refresh", {"project", ""}},
        {"cysynck.network.publish", {"project", "project"}},
        {"cysynck.network.discover", {"project", "project"}},
        {"cysynck.network.trust",
         {"project profile expected_key expected_fingerprint", "project profile expected_key"}},
        {"cysynck.network.revoke", {"project peer", "project peer"}},
        {"cysynck.chat.send", {"project peer text", "project peer text"}},
        {"cysynck.transfers.send", {"project peer path", "project peer path"}},
        {"cysynck.transfers.resume", {"project transfer", "project transfer"}},
        {"cysynck.transfers.cancel", {"project transfer direction", "project transfer direction"}},
        {"cysynck.transfers.configure", {"file_limit_mib inbox_quota_mib outbox_quota_mib", ""}},
        {"cysynck.cyrevision.identity", {"", ""}},
        {"cysynck.cyrevision.identity-create", {"name", "name"}},
        {"cysynck.project.cyrevision-configure", {"project_id signature_format", "project_id"}},
        {"cysynck.project.cyrevision-trust", {"identity fingerprint", "identity fingerprint"}},
        {"cysynck.project.cyrevision-revoke", {"device", "device"}},
        {"cysynck.project.cyrevision-export", {"", ""}},
        {"cysynck.project.cyrevision-import", {"transaction", "transaction"}},
        {"cysynck.project.cyrevision-recover", {"device apply plan", "device"}},
        {"cysynck.service.plan", {"platform executable target_state port", "platform executable"}},
        {"cysynck.service.export",
         {"platform executable target_state port destination plan",
          "platform executable destination plan"}},
        {"cysynck.help.location", {"page section language", ""}},
        {"cysynck.maintenance.tick", {"", ""}},
        {"cysynck.settings.get", {"", ""}},
        {"cysynck.settings.set",
         {"ui_view ui_project ui_theme ui_language ui_close_to_tray ui_start_hidden", ""}},
        {"cysynck.ui.status", {"", ""}},
        {"cysynck.ui.configure",
         {"ui_view ui_project ui_theme ui_language ui_close_to_tray ui_start_hidden", ""}},
        {"cysynck.ui.help", {"page section language", "page"}},
        {"cysynck.startup.status", {"", ""}},
        {"cysynck.startup.enable", {"", ""}},
        {"cysynck.startup.disable", {"", ""}},
        {"cysynck.receiver.status", {"", ""}},
        {"cysynck.receiver.start", {"", ""}},
        {"cysynck.receiver.stop", {"", ""}},
        {"cysynck.plugins.load", {"path expected_sha256", "path expected_sha256"}}};
    auto found = fields.find(operation);
    if (found == fields.end())
        return std::nullopt;
    Json definitions = Json::object();
    std::istringstream input(found->second.first);
    std::string key;
    while (input >> key)
        definitions[key] = {{"type", "string"}, {"maxLength", 8192}};
    auto boolean = [&](const char *field) {
        if (definitions.contains(field))
            definitions[field] = {{"type", "boolean"}};
    };
    auto integer = [&](const char *field, int min, int max) {
        if (definitions.contains(field))
            definitions[field] = {{"type", "integer"}, {"minimum", min}, {"maximum", max}};
    };
    auto enumeration = [&](const char *field, Json values) {
        if (definitions.contains(field))
            definitions[field] = {{"type", "string"}, {"enum", values}};
    };
    for (auto field :
         {"auto_start", "loopback_only", "enabled", "apply", "ui_close_to_tray", "ui_start_hidden"})
        boolean(field);
    for (auto field : {"port", "listen_port", "mesh_port"})
        integer(field, 1, 65535);
    integer("external_port", 0, 65535);
    integer("lifetime_seconds", 60, 3600);
    integer("file_limit_mib", 1, 4096);
    integer("inbox_quota_mib", 1, 65536);
    integer("outbox_quota_mib", 1, 65536);
    enumeration("action", {"enable", "renew", "disable"});
    enumeration("protocol", {"auto", "pcp", "nat-pmp"});
    enumeration("direction", {"in", "out"});
    enumeration("signature_format", {"lf", "windows-legacy"});
    enumeration("platform", {"linux", "macos"});
    enumeration("language", {"en", "fr"});
    enumeration("ui_language", {"en", "fr"});
    enumeration("ui_theme", {"mid-grey", "light-grey", "amber-night", "violet", "navy"});
    enumeration("ui_view",
                {"Overview",    "Assistant",      "Changes",          "Conflicts",    "Recovery",
                 "Backups",     "Backup scope",   "Git exchange",     "LFS cache",    "Engine",
                 "Network",     "Received files", "Direct transfers", "Activity",     "Storage",
                 "Diagnostics", "Services",       "Plugins",          "Command line", "Settings",
                 "Application"});
    if (definitions.contains("text"))
        definitions["text"]["maxLength"] = 4096;
    if (definitions.contains("api_key_env"))
        definitions["api_key_env"].update(
            {{"minLength", 1},
             {"maxLength", 256},
             {"description", "Environment variable holding the existing Syncthing API key; never "
                             "pass the secret itself"}});
    if (definitions.contains("expected_sha256"))
        definitions["expected_sha256"].update(
            {{"minLength", 64},
             {"maxLength", 64},
             {"description", "SHA-256 of the reviewed native library. Loading executes its code "
                             "for this host's lifetime"}});
    // Native Core validates all public identity fields and the independently
    // confirmed key/fingerprint. These objects never contain private keys.
    for (auto field : {"profile", "identity"})
        if (definitions.contains(field))
            definitions[field] = {{"type", "object"}, {"additionalProperties", true}};
    Json required = Json::array();
    std::istringstream needs(found->second.second);
    while (needs >> key)
        required.push_back(key);
    Json result = {{"type", "object"},
                   {"properties", definitions},
                   {"required", required},
                   {"additionalProperties", false}};
    if (definitions.contains("apply"))
        result.update(
            {{"if", {{"properties", {{"apply", {{"const", true}}}}}, {"required", {"apply"}}}},
             {"then", {{"required", {"plan"}}}}});
    return result;
}
} // namespace cysynck::assistant::schemas
