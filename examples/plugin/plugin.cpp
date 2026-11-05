#include <cysynck/plugin.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <cstring>
#include <limits>
using Json = nlohmann::json;
namespace fs = std::filesystem;
static const char *copy(const Json &value) {
    auto text = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    auto result = new char[text.size() + 1];
    std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}
static const char *report(const char *project_json, const char *) {
    try {
        auto project = Json::parse(project_json);
        const auto &options = project.at("mode_options");
        auto root = fs::u8path(project.at("root").get<std::string>());
        auto maximum = options.at("max_entries").get<size_t>();
        if (maximum < 1 || maximum > 500)
            throw std::runtime_error("Invalid report entry limit");
        Json entries = Json::array();
        size_t inspected = 0, files = 0;
        uintmax_t bytes = 0;
        bool truncated = false;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (inspected == maximum) {
                truncated = true;
                break;
            }
            ++inspected;
            if (fs::is_symlink(entry.symlink_status()) || !entry.is_regular_file())
                continue;
            auto size = entry.file_size();
            if (size > std::numeric_limits<uintmax_t>::max() - bytes)
                throw std::runtime_error("Report size overflow");
            ++files;
            bytes += size;
            if (options.at("include_names").get<bool>()) {
                auto name = entry.path().filename().u8string();
                entries.push_back(
                    {{"name", std::string(name.begin(), name.end())}, {"bytes", size}});
            }
        }
        return copy({{"state", "report-complete"},
                     {"title", options.at("title")},
                     {"project", project.at("id")},
                     {"files", files},
                     {"bytes", bytes},
                     {"inspected", inspected},
                     {"truncated", truncated},
                     {"entries", entries}});
    } catch (const std::exception &error) {
        return copy({{"state", "report-failed"}, {"error", error.what()}});
    }
}
static void release(const char *value) { delete[] value; }
static const cysynck_mode_v1 modes[] = {
    {"project-report", "Project report",
     "Read-only summary of top-level regular files; no payload hashes or recursive traversal",
     report}};
static const cysynck_plugin_v1 plugin{1, "example.report", 1, modes, nullptr, nullptr, release};
extern "C" CYSYNCK_PLUGIN_EXPORT const cysynck_plugin_v1 *cysynck_get_plugin_v1() {
    return &plugin;
}
extern "C" CYSYNCK_PLUGIN_EXPORT const char *cysynck_get_plugin_descriptor_v1() {
    return R"({"schema":1,"name":"Project report example","name_fr":"Exemple de rapport de projet","version":"1.0.0",
      "description":"Read-only example of per-project typed plugin options.","description_fr":"Exemple en lecture seule de paramètres de plugin par projet.",
      "modes":{"project-report":{"label_fr":"Rapport de projet","description_fr":"Résumé des fichiers du premier niveau, sans lecture de leur contenu.","fields":[
        {"id":"title","label":"Report title","label_fr":"Titre du rapport","type":"string","default":"Project report","max_length":160},
        {"id":"include_names","label":"Include filenames","label_fr":"Inclure les noms de fichiers","type":"boolean","default":false},
        {"id":"max_entries","label":"Maximum entries","label_fr":"Nombre maximal d'entrées","type":"integer","default":100,"minimum":1,"maximum":500}
      ]}}})";
}
