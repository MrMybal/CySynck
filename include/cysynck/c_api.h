#pragma once
/**
 * @doc c-api
 * @locale en
 * @title C ABI reference
 * @section API
 * @symbol cysynck_request
 * Include `cysynck/c_api.h`; ABI version is 1. All input and output strings use UTF-8.
 *
 * ## Functions {#functions}
 * `cysynck_open(state_path_utf8, &error_json)` returns an opaque handle, or NULL on failure.
 * NULL/empty state selects the default profile. Free any returned error even when opening fails.
 * `cysynck_request(handle, method, path, body_json, &result_json)` returns 0 on success,
 * 1 on a request/argument/JSON error, or 2 when result allocation fails. Empty/NULL body means
 * `{}`. The output may be NULL on allocation failure. Interpret the JSON report before freeing it.
 * `cysynck_load_plugin(handle, library_path_utf8, &result_json)` has the same status conventions,
 * returning the plugin catalog on success; it requires core 0.30+ and executes trusted native code.
 * `cysynck_abi_version()` returns 1. `cysynck_free(char*)` releases every returned string.
 * `cysynck_close(handle)` destroys the instance; finish all calls before closing it.
 * Hosts that need scheduled backups, engine supervision or opted-in VPN endpoint maintenance
 * periodically request `POST /api/maintenance/tick` with `{}` from a worker, not a render tick.
 * This calls the C++ `Core::tick()` and preserves C ABI version 1.
 *
 * ## Ownership {#ownership}
 * Keep the library loaded until all handles and returned strings are released.
 * Do not pass returned buffers to another allocator or close a handle concurrently with a request.
 * JSON errors cross the C request boundary instead of C++ exceptions. Native plugins are not
 * sandboxed. See the [C consumer example and Unreal notes](doc:integration).
 */
/**
 * @doc c-api
 * @locale fr
 * @title Référence de l'ABI C
 * @section API
 * @symbol cysynck_request
 * Inclure `cysynck/c_api.h` ; la version d'ABI est 1. Toutes les chaînes entrantes et sortantes
 * sont en UTF-8.
 *
 * ## Fonctions {#functions}
 * `cysynck_open(state_path_utf8, &error_json)` renvoie un handle opaque, ou NULL en cas d'échec.
 * Un chemin NULL/vide sélectionne le profil par défaut. Libérer toute erreur renvoyée même si
 * l'ouverture échoue. `cysynck_request(handle, method, path, body_json, &result_json)` renvoie 0 en
 * cas de succès, 1 pour une erreur de requête/argument/JSON, ou 2 si l'allocation du résultat
 * échoue. Un corps vide/NULL vaut `{}`. La sortie peut être NULL si l'allocation échoue.
 * Interpréter le rapport JSON avant de le libérer. `cysynck_load_plugin(handle, library_path_utf8,
 * &result_json)` suit les mêmes codes de retour, renvoie le catalogue en cas de succès, nécessite
 * le cœur 0.30+ et exécute du code natif de confiance. `cysynck_abi_version()` renvoie 1.
 * `cysynck_free(char*)` libère toutes les chaînes retournées. `cysynck_close(handle)` détruit
 * l'instance ; terminer tous les appels avant la fermeture.
 * Pour les sauvegardes planifiées, la supervision du moteur ou l'entretien VPN activé, demander
 * périodiquement `POST /api/maintenance/tick` avec `{}` depuis un travailleur, hors du rendu.
 * Cette route appelle `Core::tick()` en C++ et conserve la version 1 de l'ABI C.
 *
 * ## Propriété mémoire {#ownership}
 * Garder la bibliothèque chargée jusqu'à la libération de tous les handles et chaînes retournées.
 * Ne pas confier ces buffers à un autre allocateur ni fermer un handle pendant une requête.
 * La frontière C de requête renvoie des erreurs JSON au lieu d'exceptions C++. Les extensions
 * natives ne sont pas isolées. Voir [l'exemple C et les notes Unreal](doc:integration).
 */
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
/* Additive in core 0.30. Loads an explicitly selected trusted native library.
 * No HTTP equivalent. ABI version remains 1; use a core built with this export. */
CYSYNCK_API int cysynck_load_plugin(cysynck_handle, const char *library_path_utf8,
                                    char **result_json);
CYSYNCK_API void cysynck_free(char *value);
CYSYNCK_API void cysynck_close(cysynck_handle);
CYSYNCK_API unsigned int cysynck_abi_version(void);
#ifdef __cplusplus
}
#endif
