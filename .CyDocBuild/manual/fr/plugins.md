---
id: plugins
title: Extensions natives et options de modes typées
description: Charger des bibliothèques de confiance et développer un mode avec le SDK.
section: Développement
order: 72
parent: architecture
sourceRevision: 1
---

# Extensions natives et options de modes typées {#native-plugins-typed-modes-and-the-extension-sdk}

CySynck fournit un catalogue des extensions chargées, des options typées par projet et un point C de chargement depuis un autre hôte.
L’ABI d’extension reste version 1. Le descripteur facultatif n’est pas exigé pour les anciens plugins ; leurs identifiants/chaînes passent la validation de l’hôte.

## Charger et inspecter {#load-and-inspect}

Charger exécute du code natif avec les droits du processus.
Choisir explicitement les bibliothèques au démarrage ou par API C++/C.
Le Web liste/configure celles déjà chargées et n’offre ni chargement ni téléversement.

```sh
cysynck --plugin /absolute/path/plugin.so plugins
cysynck-desktop --plugin /absolute/path/plugin.so
```

Utiliser `.dll` sous Windows, `.dylib` sous macOS, `.so` sous Linux.
La vue **Extensions** et le catalogue Web montrent noms, versions, descriptions et libellés français facultatifs.
Les détails ajoutent ID, chemin canonique choisi et SHA-256 observé au chargement ; cette empreinte n’est ni une signature ni une mesure des bibliothèques dépendantes.
Le chargeur contrôle changements pendant chargement, doublons de bibliothèques/IDs/modes, métadonnées et ABI avant publication.
Au plus 32 bibliothèques, 100 modes chacune ; elles restent chargées jusqu’à destruction du Core.
ID de plugin : au plus 128 lettres ASCII, chiffres, points, `_`, `-`, commençant par lettre/chiffre.
IDs de modes : règles existantes de 80 caractères sans points.
Les callbacks avant/après gardent leur comportement d’événements entre projets.
Ils sont synchrones dans la requête ; ne pas rappeler la même instance ni ouvrir son profil par un autre Core pendant le callback.
GET `/api/plugins` retourne `abi_version` et `plugins`.
`overview.modes` ajoute propriétaire, descriptions, français facultatif, champs d’options et hash de schéma aux modes d’extension.
Les modes intégrés restent disponibles.
Aucune découverte, persistance comme instruction de chargement, téléchargement ou déchargement par route de requête.

## Options par projet {#per-project-options}

Ouvrir **Paramètres → Options du mode** dans CyUI, ou **Options du mode** dans le Web.
Contrôles : chaînes, booléens, entiers bornés. Les champs utilisent le français fourni, sinon l’anglais.
Recharger reprend les valeurs courantes ; **Valeurs par défaut** change seulement le brouillon ; **Enregistrer les options du mode** valide et persiste explicitement.

```sh
cysynck --plugin /path/plugin.so project mode-options PROJECT
cysynck --plugin /path/plugin.so project mode-options-set PROJECT options.json --plan TOKEN
```

`options.json` est un objet UTF-8 de valeurs explicites ; les champs absents prennent les défauts.
GET `/api/projects/PROJECT/mode-options` fournit `available`, `valid`, `plugin`, `fields`, `values`, `defaults`, `errors`, `schema_hash`, `plan`.
PUT reçoit `{"plan":"TOKEN","values":{"title":"My report","include_names":true}}`.
Le jeton lie profil, projet/racine, mode, schéma/identité de bibliothèque chargée et valeurs stockées ; changement de paramètres/plugin exige rechargement avant sauvegarde.
`mode_options` accepte aussi des valeurs validées dans POST/PUT ordinaires du projet, sans jeton d’éditeur.
Identifiant, racine et mode d’un projet existant restent immuables. Les options restent privées et locales par projet.
Défauts et surcharges résolus passent dans `project_json.mode_options` et `context_json.mode_options` lors de l’exécution.
Le contexte contient aussi `state` et `plugin`.
Des options invalides bloquent avant sauvegarde préalable et callbacks ; l’inspecteur fournit les diagnostics pour réparation explicite.
Un plugin absent conserve projet/options ; les sauvegardes séparées restent possibles.
Limites : 32 champs par mode, 32 Kio d’options résolues, chaînes de 4 096 octets UTF-8 au plus (défaut 1 024), entiers de -1 000 000 000 à 1 000 000 000.
Aucune coercition : nombre à la place d’un booléen, fraction à la place d’un entier, clé inconnue et chaîne UTF-8 invalide/NUL sont refusés.
Les contraintes déclarées plus étroites s’appliquent aussi aux défauts. Le résultat d’un mode est limité à 1 Mio.

## Export de descripteur facultatif {#optional-descriptor-export}

Garder `cysynck_get_plugin_v1` et les structures de `cysynck/plugin.h`, puis exporter facultativement :

```cpp
extern "C" CYSYNCK_PLUGIN_EXPORT
const char *cysynck_get_plugin_descriptor_v1() {
    return R"({"schema":1,"name":"Report extension","version":"1.0.0",
      "modes":{"my-mode":{"label_fr":"Mon mode","fields":[
        {"id":"title","label":"Title","label_fr":"Titre","type":"string",
         "default":"Report","max_length":160},
        {"id":"enabled","label":"Enabled","type":"boolean","default":true},
        {"id":"limit","label":"Limit","type":"integer","default":10,
         "minimum":1,"maximum":100}
      ]}}})";
}
```

Renvoyer du JSON UTF-8 immuable terminé par NUL, valide pendant la vie de la bibliothèque.
L’hôte le copie au chargement et n’appelle pas `release_string` sur ces métadonnées statiques.
L’export est facultatif ; s’il existe, NULL est une erreur.
Limites : 64 Kio et douze niveaux d’imbrication.
Champs racine : `schema`, `name`, `name_fr`, `version`, `description`, `description_fr`, `modes`.
`modes` décrit seulement les IDs réellement exportés avec `fields`, `label_fr`, `description_fr` ; libellés/descriptions anglais viennent de la structure existante.
Chaque champ a un `id` ASCII unique, `label` anglais, `label_fr` facultatif, `description`/`description_fr`, `type`, `default` et ses contraintes.
Clés inconnues, champs dupliqués, contraintes incohérentes et défauts invalides refusent toute la bibliothèque.
Points d’entrée, tableaux et chaînes restent valides pendant chargement.
La propriété des résultats reste ABI v1 : l’hôte copie résultats/refus et appelle `release_string` s’il existe, y compris en cas d’erreur de parsing/limite.
Une chaîne statique peut omettre ce callback.
Renvoyer du JSON valide et contenir les exceptions dans l’extension.
La frontière C de requête transforme les exceptions inattendues en erreurs JSON sans isoler les fautes mémoire du code natif.

## Construire une extension autonome avec le SDK installé {#build-an-independent-plugin-from-the-installed-sdk}

`share/cysynck/examples/plugin` lie `CySynck::PluginSDK`, constitué d’en-têtes, sans lier le binaire du cœur :

```sh
cmake -S PREFIX/share/cysynck/examples/plugin -B plugin-build -DCMAKE_PREFIX_PATH=PREFIX
cmake --build plugin-build --config Release
```

Son mode `project-report` inspecte un nombre borné d’entrées du premier niveau et rapporte nombre/taille des fichiers réguliers.
`include_names` ajoute les noms ; `title` et `max_entries` sont configurables.
Il ne hashe aucun contenu et ne parcourt pas les sous-dossiers.
L’exemple montre métadonnées bilingues statiques et résultats dynamiques libérés par son propre allocateur.
Compiler pour OS/architecture et convention d’appel de l’hôte ; le contrat contient des structures C et du JSON UTF-8.

## Intégrer depuis C ou C++ {#embed-from-c-or-c}

```c
char *result = NULL;
int status = cysynck_load_plugin(handle, library_path_utf8, &result);
/* status == 0 : result contient le catalogue chargé ; sinon une erreur JSON. */
cysynck_free(result);
```

Cet export nécessite le cœur **0.30+**, bien que `cysynck_abi_version()` reste 1 pour les points existants.
Utiliser `find_package(CySynck 0.30 CONFIG REQUIRED)` pour un nouveau consommateur de cette fonction.
Le C++ peut appeler `Core::load_plugin`. Sérialiser le chargement avec la vie de l’hôte, garder handle/bibliothèque jusqu’à fin des requêtes.
L’exemple C installé accepte `STATE [--plugin LIBRARY] [METHOD [PATH [JSON]]]`.
La compilation Unreal et l’exécution native Linux/macOS restent des vérifications propres aux plateformes.
