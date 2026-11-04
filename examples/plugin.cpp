#include <cysynck/plugin.h>
static const char *inspect(const char *, const char *) {
    return "{\"state\":\"inspection-complete\"}";
}
static const cysynck_mode_v1 modes[] = {
    {"inspect", "Inspect only", "Example native extension without file mutation", inspect}};
static const cysynck_plugin_v1 plugin = {1, "example.inspect", 1, modes, nullptr, nullptr, nullptr};
extern "C" CYSYNCK_PLUGIN_EXPORT const cysynck_plugin_v1 *cysynck_get_plugin_v1() {
    return &plugin;
}
