---
id: integration
title: Intégrer le cœur C++ ou l’ABI C
description: Lier le SDK, gérer les chaînes UTF-8 et préparer une intégration Unreal.
section: Développement
order: 71
parent: architecture
sourceRevision: 1
---

# Intégrer le cœur C++ ou l’ABI C {#integrate-the-c-core-or-c-abi}

Le paquet contient la bibliothèque partagée C++20, les en-têtes et un package CMake déplaçable.
Desktop et serveur exposent les mêmes API.
Les exemples sont installés sous `share/cysynck/examples/consumer` et `share/cysynck/examples/c-consumer`.
La [référence C++](doc:cpp-api), la [référence C](doc:c-api) et [l’ABI des extensions](doc:plugin-api) sont extraites d’annotations explicites des en-têtes publics distribués.
L’index ne publie pas les corps d’implémentation et n’infère pas les surcharges ou macros avec un analyseur C++.

## C++20 {#c20}

```cmake
find_package(CySynck CONFIG REQUIRED)
target_compile_features(MyApp PRIVATE cxx_std_20)
target_link_libraries(MyApp PRIVATE CySynck::Core)
```

```cpp
#include <cysynck/core.hpp>
cysynck::Core core(profile_path);
auto report = core.request("GET", "/api/doctor");
```

Compiler les consommateurs C++ avec un compilateur, runtime et ABI de bibliothèque standard compatibles.
Un build croisé Zig/libc++ n’est notamment pas interchangeable avec un consommateur natif libstdc++.
L’ABI C évite d’exposer ces types à travers la bibliothèque.
Conserver l’instance pendant les appels et effectuer les travaux bloquants hors du rendu.
Un hôte souhaitant moteur supervisé/sauvegardes planifiées doit appeler régulièrement `Core::tick()`.

L’exemple C++ utilise un point d’entrée Windows large pour conserver les chemins accentués. Sur POSIX, les octets des chemins en ligne de commande sont interprétés en UTF-8. L’hôte doit fournir un `std::filesystem::path` natif ou convertir explicitement l’UTF-8, sans assimiler les arguments Windows `char*` encodés selon la page locale à de l’UTF-8.

## ABI C, version 1 {#c-abi-version-1}

Depuis 0.30, `cysynck_load_plugin(handle, library_path_utf8, &result_json)` s’ajoute sans changer la version 1 des points existants.
Cette fonction nécessite le cœur 0.30+.
La cible d’en-têtes `CySynck::PluginSDK` et l’exemple autonome servent aux auteurs d’extensions ; voir [options typées et propriété](doc:plugins).
Parcours des snapshots, restauration sélective et comparaison de versions passent par la requête JSON existante sans changement d’ABI ; voir [les workflows](doc:snapshot-workflows).
Inclure seulement `cysynck/c_api.h`. Handles opaques et JSON UTF-8 s’utilisent depuis C, Unreal ou une autre application C++ sans partager de types STL.
Vérifier `cysynck_abi_version()` avant d’ouvrir un profil.

```c
char *result = NULL;
cysynck_handle core = cysynck_open(state_path_utf8, &result);
if (!core) {
    /* result peut contenir une erreur JSON allouée ; gérer aussi un échec d’allocation. */
    cysynck_free(result);
    return 1;
}
int status = cysynck_request(core, "GET", "/api/doctor", "{}", &result);
/* status == 0 indique un succès ; une valeur non nulle signale un échec. */
/* Lire result avant de le libérer ; il peut être NULL si son allocation échoue. */
cysynck_free(result);
cysynck_close(core);
```

Libérer chaque chaîne, erreurs comprises, avec `cysynck_free`, jamais avec l’allocateur de l’hôte.
Garder le handle vivant jusqu’à fin des appels, sans fermeture concurrente.
La bibliothèque reste chargée jusqu’à libération de tous les handles/buffers.
Les chemins relatifs et identifiants suivent les mêmes règles que le CLI ; la frontière C renvoie des erreurs JSON au lieu d’exceptions C++.
La sortie peut être NULL si son allocation échoue.
L’exemple C accepte `STATE [--plugin LIBRARY] [METHOD [API_PATH [JSON_BODY]]]`.
Son entrée Windows convertit les arguments larges en UTF-8, y compris les chemins accentués.
Il active seulement le langage C dans CMake et lie `CySynck::Core` avec le linker C.

```sh
cmake -S PREFIX/share/cysynck/examples/c-consumer -B build-c-consumer \
  -DCMAKE_PREFIX_PATH=PREFIX -DCMAKE_BUILD_TYPE=Release
cmake --build build-c-consumer --config Release
```

Sous Windows, garder la DLL à côté de l’exécutable ; l’exemple la copie.
Sur POSIX les exemples utilisent l’emplacement enregistré par CMake.
Votre distribution exige sa stratégie de chargement/déploiement et des essais sur le vrai système cible.
Le pont `integrations/unreal/CySynck` nécessite encore une compilation/exécution réelle dans Unreal ; voir [les consignes Unreal](doc:architecture#autres-applications-c-et-unreal).

Les opérations suivantes restent accessibles via la même requête JSON, ABI version 1 :

- [Intégration initiale/avance rapide](doc:git-integration) : aperçu, restrictions de fichiers, reçus et revue d’interruption par `git-integrations`, `git-integration-check`, `git-integration-acknowledge`.
- Depuis 0.23, `git-merge-review`/`git-merge-file` : revue commitée à trois versions et propositions bornées ; fixer commits, référence locale et ancêtre, sans application sur ces routes.
- Depuis 0.24, [git-merge](doc:git-divergent-merge) : résultat complet, identité, application par jeton et commit à deux parents avec reçu.
- Depuis 0.25, `resolutions` choisit un fichier `local`/`peer`, suppression comprise, par `{path,side}`. Répéter le tableau pour appliquer ; `result_ready` distingue candidat complet et conflits restants.
- Depuis 0.26, `{path,side:"edited",content:"combined text\n"}` ajoute le texte UTF-8 avec contrôles taille/marqueurs/permissions avant publication d’objets.
  Les mêmes octets sont requis à l’application et gardés dans le reçu. Voir [l’édition](doc:git-divergent-merge#editing-conflict-text-026).
- Depuis 0.27, les régions de `git-merge-file` acceptent `region_token` et `region_choices` ordonnés `local`/`peer`/`both` pour régénérer `composed_text` en lecture seule.
  Soumettre ensuite le texte examiné comme résolution `edited` à l’aperçu/application normal ; voir [les régions](doc:git-divergent-merge#choosing-individual-conflict-regions-027).
- Depuis 0.28, [la matérialisation locale](doc:git-materialization) vérifie les pointeurs/cache et expose catalogue, contrôle et revue des reçus sans changer HEAD/index.
- Les sauvegardes `backup_storage:"cdc"` utilisent `backup`, `verify`, `restore-plan`, `restore` existants ; les recettes exigent 0.22+. Voir [les formats](doc:block-backups).
- Depuis 0.31, [périmètre de sauvegarde et exclusions](doc:backup-scope) utilisent les aperçus et écritures vérifiées.
- Depuis 0.32, [cache LFS](doc:git-cache) : inventaire, rechargement et reçus hors ligne.
- Depuis 0.33, `git-lfs-recovery-check`/`git-lfs-recovery-acknowledge` contrôlent les objets et examinent une interruption avec jeton frais en gardant ancien statut et compteurs installés.
- Depuis 0.34, `lfs_policy:"pointers"` sur `git-integrate` conserve les pointeurs au premier checkout/avance rapide, avec `lfs_before`/`lfs_after` ; voir [la politique](doc:git-lfs-integration).
- Les sélections/reprises de matérialisation, [la restauration des pointeurs](doc:git-dehydration) et [l’export préservé](doc:git-asset-export) restent des opérations JSON de l’ABI v1.
