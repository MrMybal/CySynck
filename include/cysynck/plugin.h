#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define CYSYNCK_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CYSYNCK_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* UTF-8 JSON. Returned strings belong to the plugin until release_string(). */
typedef const char *(*cysynck_mode_run_v1)(const char *project_json, const char *context_json);
typedef struct cysynck_mode_v1 {
    const char *id;
    const char *label;
    const char *description;
    cysynck_mode_run_v1 run;
} cysynck_mode_v1;
typedef struct cysynck_plugin_v1 {
    uint32_t abi_version;
    const char *id;
    size_t mode_count;
    const cysynck_mode_v1 *modes;
    /* A non-empty error returned by before_event vetoes the operation. */
    const char *(*before_event)(const char *event, const char *context_json);
    void (*after_event)(const char *event, const char *context_json);
    void (*release_string)(const char *);
} cysynck_plugin_v1;
typedef const cysynck_plugin_v1 *(*cysynck_plugin_entry_v1)(void);
CYSYNCK_PLUGIN_EXPORT const cysynck_plugin_v1 *cysynck_get_plugin_v1(void);
#ifdef __cplusplus
}
#endif
