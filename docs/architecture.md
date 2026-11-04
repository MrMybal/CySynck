# Architecture C++

`cysynck_core` est une bibliothèque partagée C++20 sans dépendance graphique.
Le desktop et le CLI appellent `Core::request(method, path, json)` ; le serveur expose
la même surface par HTTP. L'interface contient seulement les états de présentation
et les appels asynchrones. Un serveur Linux se compile avec `CYSYNCK_DESKTOP=OFF`.

## Structure

- `include/cysynck/core.hpp` : API C++.
- `include/cysynck/c_api.h` : ABI C version 1, allocations libérées dans la bibliothèque qui les possède.
- `include/cysynck/plugin.h` : contrat des extensions natives.
- `src/core.cpp` : projets, capture des snapshots, diff et échange Git.
- `src/backup.cpp` : validation des manifests, intégrité, restauration et collecte d'objets.
- `src/diff.cpp` : comparaison bornée, patches unifiés et découverte des copies de conflit.
- `src/resolution.cpp` : plans de résolution, copies privées, publication exclusive et récupération.
- `src/process.cpp` : exécution sans shell, arguments UTF-8 et délais limites.
- `src/child_process.cpp` : enfant supervisé, environnement dédié et propriété du processus.
- `src/engine.cpp` : configuration isolée Syncthing, supervision et appareils autorisés.
- `src/network.cpp` : WireGuard, profils publics, appairage et transferts directs.
- `src/http.cpp` : API locale, canal direct et adaptation REST Syncthing.
- `src/commands.cpp` : commandes partagées entre le CLI externe et le panneau desktop.
- `src/desktop.cpp` : application GLFW/OpenGL, CyUI Core et CyUI Diff.
- `src/desktop_support.cpp` : verrou de session desktop et demandes locales de réouverture.
- `src/startup.cpp` : inscriptions locales au démarrage, sans dépendance graphique.
- `src/tray_windows.cpp`, `tray_linux.cpp`, `tray_macos.mm` : menu de notification natif.

## État persistant

Le dossier d'état contient `registry.json`, les manifests `snapshots/<project>/<id>.json`,
les objets `objects/<sha256>`, le jeton `api-token`, l'identité privée `vpn/` et les fichiers
reçus `inbox/`. Le registre est remplacé atomiquement. Les opérations sont sérialisées
par un mutex et un verrou de fichier entre processus. Les transferts sortants relâchent
ces verrous pendant les communications afin de permettre la réception simultanée.

`engine/api-key` conserve la clé REST privée ; `engine/syncthing/` contient les certificats
et la configuration du moteur géré. Le dossier d'état est limité à son propriétaire
(avec SYSTEM et Administrateurs sous Windows). `engine/owner.lock` est détenu pendant
toute la vie de l'enfant et empêche deux contrôleurs de le démarrer simultanément.
Le superviseur ne termine que son enfant, sans recherche globale de processus par nom.
Le journal Syncthing est limité à 1 Mio et deux anciennes rotations.

Les fichiers sont lus en blocs pendant une sauvegarde. Un changement de taille ou de date
pendant la lecture provoque une nouvelle tentative, puis un échec visible. Le manifest
n'est publié qu'après capture de tous les fichiers. Il ne garantit pas la cohérence
transactionnelle de fichiers modifiés ensemble. Les métadonnées de restauration actuelles
comprennent le contenu et les permissions simples ; ACL, attributs étendus, liens,
dossiers vides et dates originales ne sont pas restaurés.

## API locale

Toutes les routes `/api/*` requièrent `Authorization: Bearer <token>`.
La console Web demande ce token, disponible avec `cysynck api-token` ; elle le conserve
dans le stockage de session du navigateur. Aucune clé privée WireGuard n'est renvoyée par l'API.
La configuration privée est écrite localement par `vpn write-config`.
Les activations de tunnel sont refusées par le serveur HTTP ; elles restent locales.
La sélection d'un exécutable et la saisie d'une connexion Syncthing sont également locales :
`engine configure` et `engine connect` sont refusés sur HTTP, y compris via `/api/cli`.
Le serveur peut démarrer/arrêter son moteur préconfiguré. Les routes de statut n'exposent
pas sa clé REST. `engine connect` lit `CYSYNCK_CONNECT_API_KEY` dans le CLI local.

Routes principales :

| Route | Méthode |
| --- | --- |
| `/api/overview`, `/api/events` | GET |
| `/api/storage` | GET |
| `/api/storage/prune` | POST, aperçu par défaut ; application avec `{ "apply": true, "plan": "..." }` |
| `/api/engine/status`, `devices`, `log` | GET |
| `/api/engine/configure`, `connect`, `start`, `stop`, `add-device`, `share` | POST |
| `/api/projects` | POST |
| `/api/projects/<id>` | PUT |
| `/api/projects/<id>/snapshots` | GET |
| `/api/projects/<id>/backup`, `changes`, `diff`, `verify`, `restore-plan`, `restore`, `register`, `sync`, `status`, `pause` | POST |
| `/api/projects/<id>/compare`, `conflicts`, `conflict-diff` | POST |
| `/api/projects/<id>/resolution-plan`, `resolve-conflict`, `resolutions`, `resolution-export`, `resolution-acknowledge` | POST |
| `/api/projects/<id>/git-status`, `export`, `inbox`, `import` | POST |
| `/api/projects/<id>/git-setup` (preview, then explicit initialization with plan) | POST |
| `/api/network/status` | GET |
| `/api/network/configure`, `publish`, `discover`, `trust`, `revoke`, `preview`, `write-config` | POST |
| `/api/network/chat`, `send-file`, `history` | POST |
| `/api/cli` | POST avec `{ "args": ["project", "list"] }` |

Un accès distant se fait par tunnel SSH : le CLI accepte `--server http://127.0.0.1:8484`
et lit le token dans `CYSYNCK_API_TOKEN`. Le desktop v0.8 utilise un cœur local.

Les plans de résolution lient le profil, le projet, les chemins, les empreintes complètes,
la taille, la date et les permissions simples, ainsi que le choix de contenu. Ils sont
recalculés sous le verrou d'opérations avant application. Le dossier Syncthing correspondant
doit être en pause, avec `.cysynck-recovery` exclu avant la création des archives de travail.
`resolutions/<project>/<id>/` conserve un journal JSON et les copies privées vérifiées.
`registry.json.resolution_pending` signale une opération interrompue ; aucune reprise
automatique ne remplace un fichier de travail. Les erreurs natives sont normalisées en UTF-8
avant leur enregistrement, y compris lors d'un refus de fichier verrouillé Windows.

`compare` attend `{ "left": "relative/file.txt", "right": "relative/other.txt" }` et
exige deux fichiers réguliers du projet. `conflicts` retourne `files`, `errors`, `count`
et `truncated`. `conflict-diff` attend `{ "path": "relative/conflict-name.txt" }`.
Les résultats de comparaison incluent `before_info` et `after_info` (existence, taille,
SHA-256 lorsque lu), puis pour le texte `before`, `after`, `patch`, `added_lines`,
`removed_lines`, `hunks` et `approximate`. `patch_available` indique l'exportabilité.
`binary: true` signifie qu'aucun aperçu texte n'est disponible, y compris si une limite
de taille ou de lignes est dépassée. `identical: null` signifie que le contenu n'a pas
été entièrement lu. Ces routes ne modifient aucun fichier du projet.

`verify` accepte `{ "snapshot": "id" }`, ou utilise la dernière sauvegarde. Une corruption
d'objet produit un résultat `ok: false` avec `issues`, sans exception HTTP ; un manifest
invalide produit une erreur. `restore-plan` accepte `snapshot` et `destination`, vérifie
les objets et renvoie aussi `entries`. Il ne crée pas le dossier. La restauration contrôle
de nouveau l'intégrité avant de créer exclusivement un nouveau dossier.

La collecte utilise le verrou d'opérations commun à tous les processus. Elle recense chaque
manifest sur disque et conserve l'union de leurs références SHA-256. Les erreurs de catalogue
interdisent toute suppression. Le jeton d'aperçu est lié au profil, aux manifests et à
l'inventaire des objets (nom, taille, date). Une modification invalide l'application du plan.
Seuls les fichiers réguliers portant un nom SHA-256 et sans référence sont supprimés,
individuellement ; les fichiers temporaires ou non reconnus restent conservés.

Les inscriptions à l'ouverture de session appartiennent au module desktop/CLI local
`cysynck_startup`, pas au cœur partagé. Elles ne peuvent pas être modifiées via l'API Web.
Chaque entrée est identifiée par le profil et porte un marqueur de propriété. Une installation
ne nettoie que ses propres raccourcis Windows. Le SDK et le serveur n'ont pas de dépendance
graphique liée à ce module.

## Extensions natives

Compiler `examples/plugin.cpp` en bibliothèque partagée. La charger explicitement :

```sh
cysynck --plugin /path/to/plugin.so modes
cysynck-desktop --plugin /path/to/plugin.so
```

L'extension exporte `cysynck_get_plugin_v1` et conserve ses descripteurs tant qu'elle est chargée.
Elle peut ajouter des modes, refuser une sauvegarde ou une synchronisation via les événements
`before_backup`/`before_sync`, et observer `after_backup`. Un mode reçoit les paramètres du
projet et le chemin d'état ; son résultat doit être un objet JSON valide.

L'ABI n'expose ni STL ni objets Unreal. Les callbacks ne doivent jamais laisser traverser
d'exception C++, appeler récursivement le cœur, ni modifier les manifests pendant un événement.
Les extensions sont du code natif de confiance : elles ont les droits du processus.
Elles ne sont pas téléchargées ou chargées depuis les dossiers synchronisés automatiquement.

## Autres applications C++ et Unreal

Après `cmake --install`, `examples/consumer` peut être compilé avec
`-DCMAKE_PREFIX_PATH=/path/to/dist`. Pour une intégration compilée avec un autre outil ou
une autre version du compilateur, privilégier `c_api.h` et l'ABI C.

Pour Unreal : copier `integrations/unreal/CySynck` dans `Plugins/` du projet, puis installer
la bibliothèque native dans `Plugins/CySynck/ThirdParty/CySynck` avec `cmake --install --prefix`.
Le module vérifie l'ABI, charge les fonctions C et expose `Open`, `Request`, `Close` aux Blueprints.
Il ne dépend pas de CyUI et n'embarque pas une seconde instance ImGui dans Unreal.

Les requêtes de sauvegarde, diff et réseau doivent être exécutées dans une tâche appropriée,
pas sur le tick de rendu. Le pont fourni est synchrone ; il ne fournit pas encore de nœud
Blueprint asynchrone ni de scheduler automatique. Il doit être compilé et validé avec
la version d'Unreal visée avant intégration à un produit.
