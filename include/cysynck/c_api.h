#pragma once
#if defined(_WIN32)
#if defined(CYSYNCK_BUILDING_CORE)
#define CYSYNCK_API __declspec(dllexport)
#else
#define CYSYNCK_API __declspec(dllimport)
#endif
#else
#define CYSYNCK_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef void *cysynck_handle;
/* All strings are UTF-8. Never let exceptions cross this ABI. Free every
 * output. */
CYSYNCK_API cysynck_handle cysynck_open(const char *state_path_utf8, char **error_json);
CYSYNCK_API int cysynck_request(cysynck_handle, const char *method, const char *path,
                                const char *body_json, char **result_json);
CYSYNCK_API void cysynck_free(char *value);
CYSYNCK_API void cysynck_close(cysynck_handle);
CYSYNCK_API unsigned int cysynck_abi_version(void);
#ifdef __cplusplus
}
#endif
