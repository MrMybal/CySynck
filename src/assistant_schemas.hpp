#pragma once
#include <cysynck/core.hpp>
#include <map>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include "assistant_action_schemas.hpp"

namespace cysynck::assistant::schemas {
inline bool preview(const std::string &name, const Json &arguments) {
    if (!arguments.is_object())
        return false;
    static const std::vector<std::string> actions = {"cysynck.storage.prune",
                                                     "cysynck.project.git-setup",
                                                     "cysynck.project.git-extract",
                                                     "cysynck.project.git-integrate",
                                                     "cysynck.project.git-merge",
                                                     "cysynck.project.git-materialize",
                                                     "cysynck.project.git-materialization-resume",
                                                     "cysynck.project.git-materialization-export",
                                                     "cysynck.project.git-lfs-recover",
                                                     "cysynck.project.git-dehydrate",
                                                     "cysynck.project.git-storage",
                                                     "cysynck.project.cyrevision-recover"};
    if (std::find(actions.begin(), actions.end(), name) == actions.end())
        return false;
    const auto data = arguments.value("parameters", Json::object());
    return data.is_object() && (!data.contains("apply") ||
                                (data.at("apply").is_boolean() && !data.at("apply").get<bool>()));
}
inline Json output(const std::string &name) {
    Json result = {{"description",
                    "Exact native Core result; mode-specific data and review blockers retain "
                    "the domain contract documented in the offline guide"}};
    if (name == "cysynck.overview")
        result.update({{"type", "object"},
                       {"properties",
                        {{"version", {{"type", "string"}}},
                         {"projects", {{"type", "array"}}},
                         {"modes", {{"type", "array"}}}}},
                       {"required", {"version", "projects", "modes"}}});
    else if (name == "cysynck.events" || name == "cysynck.project.snapshots")
        result["type"] = "array";
    else if (name == "cysynck.project.create" || name == "cysynck.project.update")
        result.update({{"type", "object"},
                       {"properties",
                        {{"id", {{"type", "string"}}},
                         {"name", {{"type", "string"}}},
                         {"root", {{"type", "string"}}},
                         {"mode", {{"type", "string"}}}}},
                       {"required", {"id", "name", "root", "mode"}}});
    else if (name == "cysynck.project.backup")
        result.update({{"type", "object"},
                       {"properties",
                        {{"id", {{"type", "string"}}},
                         {"file_count", {{"type", "integer"}}},
                         {"bytes", {{"type", "integer"}}}}},
                       {"required", {"id", "file_count", "bytes"}}});
    else if (name == "cysynck.project.snapshot-files")
        result.update(
            {{"type", "object"},
             {"properties",
              {{"snapshot", {{"type", "string"}}},
               {"manifest_hash", {{"type", "string"}}},
               {"files", {{"type", "array"}}},
               {"total", {{"type", "integer"}}},
               {"offset", {{"type", "integer"}}},
               {"has_more", {{"type", "boolean"}}}}},
             {"required", {"snapshot", "files", "total", "offset", "has_more", "manifest_hash"}}});
    else if (name == "cysynck.project.verify")
        result.update({{"type", "object"},
                       {"properties", {{"ok", {{"type", "boolean"}}}}},
                       {"required", {"ok"}}});
    return {{"type", "object"},
            {"properties", {{"result", result}}},
            {"required", {"result"}},
            {"additionalProperties", false}};
}
inline Json parameters(const std::string &operation) {
    if (auto extended = extended_parameters(operation))
        return *extended;
    auto string = [](const char *description = "") {
        return Json{{"type", "string"}, {"maxLength", 8192}, {"description", description}};
    };
    auto strings = [&] {
        return Json{{"type", "array"}, {"items", string()}, {"maxItems", 10000}};
    };
    Json definitions = Json::object();
    for (auto field :
         {"plan",           "path",       "snapshot",    "left",        "right",
          "prefix",         "peer",       "branch",      "destination", "manifest_hash",
          "inventory_hash", "left_hash",  "right_hash",  "base",        "head",
          "local_ref",      "merge_base", "commit",      "receipt",     "resolution",
          "transfer",       "project",    "source_peer", "bundle",      "region_token"})
        definitions[field] = string();
    definitions["path"]["description"] =
        "Project-relative file path; Core validates traversal and links";
    definitions["destination"]["description"] =
        "New absolute destination; existing data is preserved";
    definitions["plan"]["description"] = "Exact token from a fresh native preview/check";
    for (auto field : {"apply", "lfs", "verify", "probe"})
        definitions[field] = {{"type", "boolean"}};
    definitions["offset"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}};
    definitions["limit"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}};
    definitions["parent"] = {{"type", "integer"}, {"minimum", 1}};
    for (auto field : {"paths", "exports", "objects", "excludes"})
        definitions[field] = strings();
    definitions["choice"] = {{"type", "string"}, {"enum", {"current", "conflict", "merged"}}};
    definitions["content"] = {{"type", "string"}, {"maxLength", 1024 * 1024}};
    definitions["object_format"] = {{"type", "string"}, {"enum", {"sha1", "sha256"}}};
    definitions["lfs_policy"] = {{"type", "string"}, {"enum", {"reject", "pointers"}}};
    definitions["region_choices"] = {
        {"type", "array"},
        {"maxItems", 256},
        {"items", {{"type", "string"}, {"enum", {"local", "peer", "both"}}}}};
    definitions["resolutions"] = {
        {"type", "array"},
        {"maxItems", 10000},
        {"items",
         {{"type", "object"},
          {"properties",
           {{"path", string()},
            {"side", {{"type", "string"}, {"enum", {"local", "peer", "edited"}}}},
            {"content", definitions["content"]}}},
          {"required", {"path", "side"}},
          {"additionalProperties", false}}}};
    definitions["values"] = {
        {"type", "object"},
        {"additionalProperties", true},
        {"description", "Values validated against the active plugin's schema from mode-options"}};
    const std::string prefix = "cysynck.project.";
    auto name = operation.starts_with(prefix) ? operation.substr(prefix.size()) : operation;
    static const std::map<std::string, std::string> fields = {
        {"cysynck.overview", ""},
        {"cysynck.events", ""},
        {"cysynck.plugins", ""},
        {"cysynck.storage.status", ""},
        {"cysynck.doctor", "probe"},
        {"cysynck.engine.status", ""},
        {"cysynck.engine.devices", ""},
        {"cysynck.engine.log", ""},
        {"cysynck.network.status", ""},
        {"cysynck.network.history", "project"},
        {"cysynck.transfers.limits", ""},
        {"cysynck.transfers.list", "project"},
        {"cysynck.storage.prune", "apply plan"},
        {"backup-preview", "excludes inventory_hash offset limit"},
        {"backup", "plan"},
        {"changes", "snapshot"},
        {"diff", "path snapshot"},
        {"compare", "left right"},
        {"conflicts", ""},
        {"conflict-diff", "path"},
        {"resolution-plan", "path choice content"},
        {"resolve-conflict", "path choice content plan"},
        {"resolutions", ""},
        {"resolution-export", "resolution destination"},
        {"resolution-acknowledge", "resolution"},
        {"verify", "snapshot paths"},
        {"snapshots", ""},
        {"snapshot-files", "snapshot prefix offset limit manifest_hash"},
        {"snapshot-compare", "left right prefix offset limit left_hash right_hash"},
        {"snapshot-compare-file", "left right path left_hash right_hash"},
        {"restore-plan", "snapshot destination paths"},
        {"restore", "snapshot destination paths plan"},
        {"git-status", ""},
        {"git-branches", ""},
        {"inbox", ""},
        {"export", "lfs"},
        {"import", "peer bundle"},
        {"git-review", "peer branch base head local_ref"},
        {"git-review-file", "peer branch path base head local_ref"},
        {"git-history", "peer branch base head local_ref offset limit"},
        {"git-commit", "peer branch commit parent base head local_ref"},
        {"git-commit-file", "peer branch commit path parent base head local_ref"},
        {"git-merge-review", "peer branch base head local_ref merge_base"},
        {"git-merge-file",
         "peer branch path base head local_ref merge_base region_choices region_token"},
        {"git-lfs-cache", "peer branch verify offset limit inventory_hash"},
        {"git-integrations", "offset limit"},
        {"git-integration-check", "receipt"},
        {"git-materializations", "offset limit"},
        {"git-materialization-check", "receipt"},
        {"git-lfs-recoveries", "offset limit"},
        {"git-lfs-recovery", "receipt"},
        {"git-lfs-recovery-check", "receipt"},
        {"received-files", ""},
        {"received-verify", "transfer"},
        {"received-export", "transfer destination"},
        {"register", ""},
        {"sync", ""},
        {"status", ""},
        {"pause", ""},
        {"resume", ""},
        {"git-setup", "branch object_format apply plan"},
        {"git-extract", "peer branch destination commit base head local_ref lfs apply plan"},
        {"git-integrate", "peer branch base head local_ref lfs_policy apply plan"},
        {"git-merge", "peer branch base head local_ref resolutions apply plan"},
        {"git-materialize", "paths apply plan"},
        {"git-materialization-resume", "receipt apply plan"},
        {"git-materialization-export", "receipt destination paths apply plan"},
        {"git-materialization-acknowledge", "receipt plan"},
        {"git-lfs-recover", "source_peer peer branch objects apply plan"},
        {"git-lfs-recovery-acknowledge", "receipt plan"},
        {"git-integration-acknowledge", "receipt plan"},
        {"git-dehydrate", "paths apply plan"},
        {"git-storage", "exports apply plan"},
        {"mode-options", ""},
        {"mode-options-set", "values plan"},
        {"backup-excludes", "excludes plan"},
        {"cyrevision-status", ""},
        {"cyrevision-inbox", ""}};
    static const std::map<std::string, std::string> required = {
        {"cysynck.network.history", "project"},
        {"cysynck.transfers.list", "project"},
        {"diff", "path"},
        {"compare", "left right"},
        {"conflict-diff", "path"},
        {"resolution-plan", "path choice"},
        {"resolve-conflict", "path choice plan"},
        {"resolution-export", "resolution destination"},
        {"resolution-acknowledge", "resolution"},
        {"snapshot-files", "snapshot"},
        {"snapshot-compare", "left right"},
        {"snapshot-compare-file", "left right path"},
        {"restore-plan", "snapshot destination"},
        {"restore", "snapshot destination"},
        {"import", "peer bundle"},
        {"git-review", "peer branch"},
        {"git-review-file", "peer branch path"},
        {"git-history", "peer branch"},
        {"git-commit", "peer branch commit"},
        {"git-commit-file", "peer branch commit path"},
        {"git-merge-review", "peer branch"},
        {"git-merge-file", "peer branch path"},
        {"git-extract", "peer branch destination"},
        {"git-integrate", "peer branch"},
        {"git-merge", "peer branch"},
        {"git-integration-check", "receipt"},
        {"git-materialization-check", "receipt"},
        {"git-lfs-recovery", "receipt"},
        {"git-lfs-recovery-check", "receipt"},
        {"received-verify", "transfer"},
        {"received-export", "transfer destination"},
        {"git-materialization-resume", "receipt"},
        {"git-materialization-export", "receipt destination"},
        {"git-materialization-acknowledge", "receipt plan"},
        {"git-lfs-recover", "source_peer"},
        {"git-lfs-recovery-acknowledge", "receipt plan"},
        {"git-integration-acknowledge", "receipt plan"},
        {"mode-options-set", "values plan"},
        {"backup-excludes", "excludes plan"}};
    Json properties = Json::object(), needs = Json::array();
    bool extra = false;
    if (name == "create" || name == "update") {
        properties = {
            {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 160}}},
            {"root", string("Existing absolute project directory, separate from the profile")},
            {"id", string()},
            {"mode", string("Built-in or installed plugin mode ID from the overview")},
            {"backup_interval_minutes", {{"type", "integer"}, {"minimum", 0}, {"maximum", 525600}}},
            {"backup_keep", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}}},
            {"backup_storage", {{"type", "string"}, {"enum", {"auto", "whole", "cdc"}}}},
            {"backup_before_sync", {{"type", "boolean"}}},
            {"paused", {{"type", "boolean"}}},
            {"excludes", strings()},
            {"exchange_path", string()},
            {"git_lfs", {{"type", "boolean"}}},
            {"git_lfs_storage", string()},
            {"mode_options", definitions["values"]}};
        if (name == "create")
            needs = {"name", "root"};
        extra = true; // Preserve native project metadata and installed plugin extensions.
    } else {
        auto found = fields.find(name);
        if (found == fields.end())
            throw std::logic_error("Missing native parameter schema: " + operation);
        std::istringstream list(found->second);
        std::string key;
        while (list >> key)
            properties[key] = definitions.at(key);
        auto must = required.find(name);
        if (must != required.end()) {
            std::istringstream list_required(must->second);
            while (list_required >> key)
                needs.push_back(key);
        }
    }
    Json schema = {
        {"type", "object"},
        {"properties", properties},
        {"required", needs},
        {"additionalProperties", extra},
        {"description",
         "Native CySynck parameters. Core validates current state, hashes, paths, quotas and "
         "plugin values. See the operation's offline guide for its domain result contract."}};
    if (properties.contains("apply"))
        schema.update(
            {{"if", {{"properties", {{"apply", {{"const", true}}}}}, {"required", {"apply"}}}},
             {"then", {{"required", {"plan"}}}}});
    return schema;
}
} // namespace cysynck::assistant::schemas
