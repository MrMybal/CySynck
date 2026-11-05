#include <cysynck/plugin.h>
#include <nlohmann/json.hpp>
#include <cstring>
using Json = nlohmann::json;
#ifndef PLUGIN_FIXTURE_KIND
#define PLUGIN_FIXTURE_KIND 0
#endif
static int released = 0;
static const char *owned(const std::string &value) {
    auto result = new char[value.size() + 1];
    std::memcpy(result, value.c_str(), value.size() + 1);
    return result;
}
static void release(const char *value) {
    ++released;
    delete[] value;
}
static const char *run(const char *project, const char *context) {
    auto p = Json::parse(project), c = Json::parse(context);
    auto behavior = p.at("mode_options").at("behavior").get<std::string>();
    if (behavior == "invalid")
        return owned("{bad-json");
    if (behavior == "oversize")
        return owned(std::string(1024 * 1024 + 1, 'x'));
    if (behavior == "throw")
        throw 7;
    return owned(Json{{"released", released},
                      {"project_options", p.at("mode_options")},
                      {"context_options", c.at("mode_options")}}
                     .dump());
}
static const char *before(const char *event, const char *context) {
    auto p = Json::parse(context);
    if (std::string(event) == "before_sync" &&
        p.value("mode_options", Json::object()).value("behavior", "valid") == "veto")
        return owned("Fixture veto");
    return nullptr;
}
#if PLUGIN_FIXTURE_KIND == 2
static const cysynck_mode_v1 modes[] = {
    {"fixture-other", "Other mode", "Duplicate plugin ID fixture", run}};
static const cysynck_plugin_v1 plugin = {1, "example.report", 1, modes, nullptr, nullptr, release};
#else
static const cysynck_mode_v1 modes[] = {
    {"fixture-memory", "Memory fixture", "Callback ownership fixture", run}};
static const cysynck_plugin_v1 plugin = {
    PLUGIN_FIXTURE_KIND == 3 ? 99u : 1u, "fixture.memory", 1, modes, before, nullptr, release};
#endif
extern "C" CYSYNCK_PLUGIN_EXPORT const cysynck_plugin_v1 *cysynck_get_plugin_v1() {
    return &plugin;
}
extern "C" CYSYNCK_PLUGIN_EXPORT const char *cysynck_get_plugin_descriptor_v1() {
#if PLUGIN_FIXTURE_KIND == 1
    return R"({"schema":1,"modes":{"fixture-memory":{"fields":[{"id":"bad","label":"Bad","type":"integer","default":0,"minimum":5,"maximum":1}]}}})";
#elif PLUGIN_FIXTURE_KIND == 2
    return R"({"schema":1})";
#else
    return R"({"schema":1,"name":"Memory fixture","version":"1","modes":{"fixture-memory":{"fields":[{"id":"behavior","label":"Behavior","type":"string","max_length":20,"default":"valid"}]}}})";
#endif
}
