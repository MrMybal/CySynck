#pragma once
/**
 * @doc plugin-api
 * @locale en
 * @title Native plugin ABI
 * @section API
 * @symbol cysynck_get_plugin_v1
 * Include `cysynck/plugin.h`; link the header-only `CySynck::PluginSDK` CMake target.
 *
 * ## Entry points {#entry-points}
 * Export `cysynck_get_plugin_v1()` returning a stable `cysynck_plugin_v1` with ABI version 1.
 * Its mode array uses `cysynck_mode_v1`: ID, English label/description and `run(project_json,
 * context_json)`. `run` returns an object encoded as UTF-8 JSON. Optional before-event callbacks
 * can veto an operation; after-event callbacks observe it. The host copies result/veto strings and
 * calls `release_string` when provided. Keep descriptors, arrays and entry-point strings valid
 * until the library is unloaded.
 *
 * ## Optional metadata {#metadata}
 * `cysynck_get_plugin_descriptor_v1()` may return immutable schema-1 UTF-8 JSON with labels and
 * typed option fields. It is optional; an existing export returning NULL is invalid. The host
 * copies it without `release_string`. Never let exceptions cross callbacks or re-enter the same
 * Core/profile from a callback. Plugins share the host's permissions and memory. See [limits,
 * metadata and the standalone example](doc:plugins).
 */
/**
 * @doc plugin-api
 * @locale fr
 * @title ABI des extensions natives
 * @section API
 * @symbol cysynck_get_plugin_v1
 * Inclure `cysynck/plugin.h` ; lier la cible CMake d'en-têtes seuls `CySynck::PluginSDK`.
 *
 * ## Points d'entrée {#entry-points}
 * Exporter `cysynck_get_plugin_v1()` renvoyant un `cysynck_plugin_v1` stable avec la version
 * d'ABI 1. Le tableau des modes utilise `cysynck_mode_v1` : identifiant, libellé/description
 * anglais et `run(project_json, context_json)`. `run` renvoie un objet encodé en JSON UTF-8. Les
 * callbacks avant événement peuvent refuser une opération ; les callbacks après événement
 * l'observent. L'hôte copie les résultats/refus et appelle `release_string` s'il est fourni.
 * Conserver descripteurs, tableaux et chaînes des points d'entrée jusqu'au déchargement de la
 * bibliothèque.
 *
 * ## Métadonnées facultatives {#metadata}
 * `cysynck_get_plugin_descriptor_v1()` peut renvoyer du JSON UTF-8 immuable de schéma 1 avec des
 * libellés et des champs typés. Cet export est facultatif ; un export présent renvoyant NULL est
 * invalide. L'hôte le copie sans `release_string`. Ne laisser traverser aucune exception et ne pas
 * rappeler le même Core/profil depuis un callback. Les extensions partagent les droits et la
 * mémoire de l'hôte. Voir [les limites, métadonnées et l'exemple autonome](doc:plugins).
 */
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
/* Optional cysynck_get_plugin_descriptor_v1 export, independent of the ABI v1 structure.
 * Return immutable UTF-8 JSON (schema 1), valid until the library is unloaded.
 * The host copies it during loading; no release_string call is made for metadata. */
typedef const char *(*cysynck_plugin_descriptor_v1)(void);
#ifdef __cplusplus
}
#endif
