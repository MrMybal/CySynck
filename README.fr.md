# CySynck

[English](README.md) · Français

Version native **C++20** d'un compagnon Syncthing organisé par projets.
Le même cœur alimente l'application **CyUI**, le CLI, le service Web et l'API d'intégration.
Le serveur se compile sans bibliothèque graphique. Aucun runtime Python ou .NET n'est requis.

Le cœur, le CLI et les identifiants d'API sont en anglais. Les interfaces desktop et Web
démarrent en anglais et proposent le français comme deuxième langue. Le choix est mémorisé
par profil pour le desktop et dans le navigateur pour le Web.

## Démarrer

La version 0.14 ajoute trois jalons Git : paramètres LFS par projet avec cache personnalisé
explicitement autorisé, revue des branches importées et des fichiers modifiés dans CyUI/Web/CLI,
puis nettoyage des exports locaux et objets LFS inutilisés après aperçu vérifié. Les données Git
locales, le cache et les objets partagés encore nécessaires sont préservés.
Voir les guides [LFS](docs/git-lfs.md), [revue Git](docs/git-review.md) et
[nettoyage Git](docs/git-storage.md) (anglais).

La version 0.13 ajoute le transport des assets Git LFS avec les bundles : historique de la branche,
versions supprimées, réutilisation des objets et vérification SHA-256 avant publication de la
branche de suivi. Le dossier de travail et l'index Git restent intacts. Les deux pairs doivent
être en 0.13 ou plus ; `git export ID --lfs false` conserve l'export ancien format, sans assets LFS.
Voir le [guide Git LFS](docs/git-lfs.md) (anglais).

La version 0.12 porte les transferts directs à 1 Gio par fichier par défaut, réglable jusqu'à
4 Gio. Les quotas de réception et d'envoi sont séparés : 4 Gio par défaut, jusqu'à 64 Gio chacun.
Le CLI, le desktop et le Web permettent de régler ces limites par profil. La copie source figée
est créée en flux, sans objet de sauvegarde supplémentaire. Voir le [guide des transferts](docs/transfers.md).

La version 0.11 ajoute la déduplication des sauvegardes par blocs de 4 Mio pour les fichiers
de 8 Mio à 256 Gio. Les blocs inchangés sont réutilisés entre versions et projets ; les anciennes
sauvegardes restent lisibles. Le mode est réglable dans le CLI, le desktop et le Web.
Voir le [guide du stockage par blocs](docs/block-backups.md) (anglais).

La version 0.10 a ajouté les transferts directs reprenables : blocs de 1 Mio vérifiés par SHA-256,
copie source figée et progression conservée après interruption. Le CLI, la vue Réseau du desktop
et la console Web permettent de reprendre un transfert. La limite initiale de 100 Mio a été relevée en 0.12.
Voir le [guide des transferts](docs/transfers.md) (anglais).

Prérequis : CMake 3.24+, Git et un compilateur C++20. Les dépendances sont récupérées par CMake.
Windows : Visual Studio 2022 avec les outils C++. Linux desktop : bibliothèques de développement
OpenGL, X11 et Wayland nécessaires à GLFW ; `libayatana-appindicator3-dev` pour la zone
de notification. macOS : Xcode Command Line Tools et CMake.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist
```

Windows : lancer `dist/bin/cysynck-desktop.exe` ou `dist/bin/cysynck.exe help`.
Linux : `dist/bin/cysynck-desktop`. macOS : `open dist/CySynck.app`.
Le CLI reste disponible avec `dist/bin/cysynck help` sur ces deux plateformes.
Les scripts `scripts/build.ps1` et `scripts/build.sh` exécutent ces étapes.

La version 0.14.0 prépare des archives `.tar.gz` pour Linux et ZIP pour Windows/macOS.
Le bundle macOS contient son cœur, ses ressources Web et ses licences. Voir le
[guide des paquets et de compilation croisée Linux](docs/packaging.md) (anglais).

L'archive Windows contient `Install-CySynck.ps1`. Après extraction, exécuter :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-CySynck.ps1
```

L'installation par utilisateur se place dans `%LOCALAPPDATA%\Programs\CySynck`,
avec des raccourcis du menu Démarrer. Elle ne nécessite pas de droits administrateur
et n'active pas le démarrage automatique. Le même script sert à mettre à jour ou
désinstaller. Voir [l'installation Windows](docs/install.md).

Pour un serveur Linux sans interface :

```sh
cmake -S . -B build-server -DCMAKE_BUILD_TYPE=Release -DCYSYNCK_DESKTOP=OFF
cmake --build build-server --parallel
./build-server/cysynck serve --host 127.0.0.1 --port 8484
```

La validation d'exécution locale porte sur Windows x64. Le cœur, le CLI et les binaires de
tests Linux x86_64 compilent aussi depuis Windows avec Zig. Le workflow CI doit encore
confirmer l'exécution native sur Linux et macOS, ainsi que les nouveaux essais des paquets.
Le pont Unreal est fourni comme exemple d'intégration et n'a pas été compilé dans Unreal ici.

## Fonctions disponibles

La vue **Diagnostic** vérifie les dossiers et dépendances selon les modes des projets.
`cysynck doctor --probe true` teste explicitement les versions des outils et la connexion
Syncthing. La vue **Services** prépare un export systemd Linux ou LaunchAgent macOS par
profil, après aperçu, avec les instructions d'installation. Elle n'active aucun service.
Ces fonctions sont aussi disponibles dans le CLI, l'API et le SDK.

Le paquet contient désormais des exemples compilables C++ et C pur. Voir les guides
[diagnostic](docs/diagnostics.md), [services](docs/services.md) et
[intégration C/C++](docs/integration.md) (anglais).

| Fonction | Comportement de la version 0.8 |
| --- | --- |
| Projets | Dossiers et paramètres séparés ; refus des racines qui se chevauchent |
| Modes | Bidirectionnel, envoi seul, réception seule, sauvegarde locale, échange Git |
| Desktop | CyUI natif avec vue d'ensemble, diff en deux colonnes, sauvegardes, Git, réseau, activité et CLI intégré |
| Arrière-plan | Icône native, fenêtre masquable, réouverture au second lancement, sauvegardes depuis le menu et arrêt explicite |
| Ouverture de session | Inscription explicite par profil, chemins et extensions conservés, retrait local depuis l'application ou le CLI |
| Sauvegardes | Objets SHA-256 dédupliqués, planification, rétention, vérification d'intégrité, aperçu et restauration dans un nouveau dossier |
| Stockage | Bilan global et nettoyage explicite des objets inutilisés, avec aperçu et protection des références de tous les projets |
| Diff | Comparaison à un snapshot ou entre deux fichiers, vue côte à côte ou unifiée, statistiques et copie d'un patch compatible Git |
| Conflits | Comparaison, choix d'une version ou fusion manuelle, aperçu vérifié, copies de récupération et historique exportable |
| Syncthing | Instance isolée supervisée, redémarrage après panne, configuration CyUI, appareils et partage ; connexion possible à une instance existante et accès au Web original |
| Git | Export de la branche commitée vers un dossier d'échange ; import dans une référence de suivi sans merge automatique |
| Extensions | Bibliothèques natives chargées explicitement, ABI C versionnée, modes supplémentaires et événements |
| VPN | Identité WireGuard locale, profils publics échangés via Syncthing, appairage par clé, configuration et activation par les outils officiels |
| Tchat/fichiers | Canal direct sur l'adresse VPN, autorisations par projet, secret d'appairage, historique local, contrôle SHA-256 des fichiers reçus |
| Fichiers reçus | Catalogue indépendant de l'historique, vérification d'intégrité et export dans un nouveau dossier séparé |
| Intégration | Bibliothèque C++, API C stable et exemple de module Unreal/Blueprint |

## Premier projet

```sh
cysynck project add Documents /srv/documents --mode backup --interval 60 --keep 30
cysynck project list
cysynck backup PROJECT_ID
cysynck changes PROJECT_ID
cysynck diff PROJECT_ID notes.txt
cysynck compare PROJECT_ID notes.txt notes-alternative.txt
cysynck conflicts PROJECT_ID
cysynck snapshots PROJECT_ID
cysynck verify PROJECT_ID --snapshot SNAPSHOT_ID
cysynck restore-plan PROJECT_ID SNAPSHOT_ID /srv/restored-copy
cysynck restore PROJECT_ID SNAPSHOT_ID /srv/restored-copy
```

Les chemins contenant des espaces doivent être entre guillemets. `--state DIR` sélectionne
un état distinct pour le CLI et le desktop. Par défaut : `%LOCALAPPDATA%/CySynck` sous Windows,
`~/Library/Application Support/CySynck` sous macOS et `$XDG_STATE_HOME/cysynck`
(ou `~/.local/state/cysynck`) sous Linux.

La planification fonctionne tant que le desktop ou `cysynck serve` reste actif.
L'intervalle `0` désactive les sauvegardes planifiées. Une pause suspend leur planification.

La vue **Sauvegardes** permet de vérifier une version et de préparer sa restauration :
la liste des fichiers, leur taille et les erreurs éventuelles apparaissent avant la création
du dossier cible. La restauration refait les contrôles et refuse les dossiers existants,
les dossiers de tous les projets et échanges, ainsi que les chemins contenant des liens.
Une erreur d'intégrité ne crée aucun dossier cible ; un problème de disque survenant ensuite
peut laisser une copie incomplète. Les fichiers sources ne sont pas modifiés.

La vue **Stockage** affiche l'espace récupérable après rétention. Le CLI offre le même aperçu :

```sh
cysynck storage status
cysynck storage prune
cysynck storage prune --apply true --plan TOKEN_RENVOYE_PAR_APERCU
```

Le nettoyage exige un aperçu encore valide et se bloque si un manifeste est endommagé.
Il préserve les objets utilisés par tous les manifests présents, y compris ceux de projets
absents du registre. Les fichiers non reconnus restent conservés. Il ne supprime ni les
snapshots retenus ni les versions internes à Syncthing. Détails : [sauvegardes](docs/backups.md).

## Application en arrière-plan

La fenêtre se ferme par défaut vers la zone de notification lorsqu'elle est disponible.
Le moteur et les sauvegardes restent actifs, sans rendu graphique de la fenêtre masquée.
Le menu de l'icône propose **Ouvrir CySynck**, **Ouvrir Syncthing Web**,
**Sauvegarder tous les projets** (les projets en pause sont ignorés) et **Quitter CySynck**.
Quitter arrête le moteur appartenant à cette application ; une instance externe reste indépendante.

La vue **Application** règle ce comportement et le lancement masqué. On peut également utiliser :

```sh
cysynck-desktop --background
cysynck-desktop --foreground
cysynck-desktop --no-tray
```

Un second lancement rouvre la fenêtre existante du même profil `--state`. Avec
`--background`, il laisse cette fenêtre dans son état actuel. Plusieurs profils restent indépendants.
Si l'icône est indisponible, la fenêtre reste accessible et sa fermeture quitte l'application.
Ces options contrôlent le lancement courant. Pour inscrire ce profil à votre prochaine
session graphique, utiliser **Application → Inscrire au démarrage**. L'état indique la
présence et la conformité de l'entrée ; le système peut la désactiver ou demander une autorisation.

```sh
cysynck --state /path/to/profile desktop startup status
cysynck --state /path/to/profile desktop startup enable
cysynck --state /path/to/profile desktop startup disable
```

L'inscription est désactivée par défaut et n'enregistre aucun secret d'environnement.
Les extensions explicitement fournies avec `--plugin` sont conservées dans les arguments.
Réinscrire après avoir déplacé l'application ou changé ces extensions. Le retrait garde
l'application en cours et les données ; il prend effet pour les prochaines connexions.

Voir [le fonctionnement du desktop](docs/desktop.md) pour les différences entre plateformes.

## Connecter Syncthing

Installer l'exécutable officiel Syncthing. Dans la vue **Moteur** du desktop, choisir
**Instance gérée**, indiquer son chemin, enregistrer puis démarrer. CySynck génère une identité,
une clé API et un dossier de configuration propres dans son état, sans reprendre les dossiers
d'une autre installation. L'option « Démarrer avec CySynck » active le lancement automatique
dans le desktop ou le service. Une panne du processus géré déclenche une tentative de reprise,
avec un délai de 30 secondes après un échec. Syncthing n'est pas inclus dans l'archive.

La même configuration est disponible dans le CLI :

```sh
cysynck engine configure --executable /opt/syncthing/syncthing --port 8385 --listen-port 22001 --auto-start true
cysynck engine run
```

`engine run` reste au premier plan ; Ctrl+C arrête son moteur. Le desktop et `serve`
conservent leur propre superviseur. Pour piloter celui du service, utiliser
`cysynck --server http://127.0.0.1:8484 engine start` (jeton API requis). Un deuxième
contrôleur peut consulter la même instance, mais seul son propriétaire peut l'arrêter.
La fermeture normale du propriétaire arrête son enfant. Sous Windows, un Job Object
termine aussi l'enfant si le propriétaire est interrompu brutalement.

L'API gérée écoute seulement sur `127.0.0.1`. Le port de transfert utilise TCP ; la découverte,
les relais et NAT Syncthing sont activés en mode normal. `--loopback-only true` limite
les transferts à cette machine et désactive ces services, notamment pour les tests.
Ces paramètres réseau sont réappliqués au démarrage du moteur géré.

Pour une instance déjà lancée, choisir **Instance existante** et saisir son URL locale
et sa clé API. La clé est conservée dans le dossier d'état privé et n'est pas renvoyée
par les routes de statut. Les anciennes variables d'environnement restent prises en charge
tant qu'aucune connexion n'a été enregistrée :

```powershell
$env:CYSYNCK_SYNCTHING_API_KEY = 'clé de votre instance'
$env:CYSYNCK_SYNCTHING_URL = 'http://127.0.0.1:8384'
.\dist\bin\cysynck-desktop.exe
```

```sh
export CYSYNCK_SYNCTHING_API_KEY='clé de votre instance'
export CYSYNCK_SYNCTHING_URL='http://127.0.0.1:8384'
cysynck project add Shared /srv/shared --mode sendreceive
cysynck register PROJECT_ID
```

Ajouter ensuite chaque appareil des deux côtés dans la vue Moteur, enregistrer le projet
dans Syncthing depuis ses paramètres, puis le partager avec l'appareil choisi. Utiliser
le même identifiant de projet sur chaque machine. L'interface Web originale reste accessible.

```sh
cysynck engine devices
cysynck engine add-device DEVICE_ID --name Portable --address dynamic
cysynck engine share PROJECT_ID DEVICE_ID
```

CySynck modifie les dossiers `cysynck-<project_id>` explicitement enregistrés. Il ne change
pas la clé GUI, la découverte ou les autres dossiers d'une instance **externe**. Un dossier est créé
en pause, l'exclusion `.git` est installée, puis son état demandé est appliqué.
Les exclusions configurables du projet concernent les **snapshots** ; les autres règles
de transfert se règlent dans les ignores Syncthing.

Cette version utilise une API HTTP locale. Pour une machine distante, utiliser un tunnel SSH
vers l'API. Ne pas exposer l'API Syncthing ou CySynck directement sur Internet.
`cysynck sync` demande une analyse ; Syncthing continue de transférer les changements en arrière-plan.

Une sauvegarde avant cette analyse couvre les actions manuelles CySynck, pas chaque réception
en arrière-plan. Le versionnement Syncthing est activé pour les nouvelles inscriptions et protège
les versions remplacées à distance. Les snapshots planifiés couvrent aussi les modifications locales.
Une sauvegarde parcourt les fichiers en cours d'utilisation : elle ne constitue pas un snapshot
transactionnel de plusieurs fichiers ou d'une base de données ouverte.

## Git collaboratif

```sh
cysynck project add Game /work/game --id game-team --mode git-bundles --exchange-path /work/game-exchange
cysynck register game-team
cysynck git export game-team
cysynck git inbox game-team
cysynck git import game-team PEER_ID BUNDLE_ID
```

Chaque appareil utilise le même identifiant de projet, avec son propre dépôt et son propre
dossier d'échange local. Syncthing partage ce dossier d'échange. La branche courante doit
avoir un commit et ne pas être en HEAD détachée. Les changements non commités restent locaux.
L'import crée `refs/remotes/cysynck/<peer>/<branch>` ; la revue, le merge et les conflits
restent des décisions Git explicites. Les commits et leurs auteurs sont conservés.

Le protocole v2 ajoute les objets Git LFS de l'historique exporté aux bundles et aux manifests
SHA-256. Les imports vérifient les assets avant de publier la branche de suivi et préservent
les modifications locales, y compris celles déjà indexées. `--lfs false` produit un bundle v1
sans les assets. Les signatures et la compatibilité CyRevision restent à implémenter.
Le dossier d'échange doit être partagé uniquement avec des membres autorisés.
Les bundles Git ne sont pas collectés automatiquement. Les objets de sauvegarde inutilisés
se nettoient explicitement depuis **Stockage** ou `storage prune` après aperçu.

## Revoir les différences et conflits

La vue **Différences** compare les fichiers à une sauvegarde, ou deux chemins relatifs
du même projet. Elle affiche les lignes ajoutées et supprimées, une vue côte à côte ou
unifiée et **Copier le patch**. Les fichiers binaires affichent leur taille et leur
empreinte lorsque la taille permet de les lire dans la limite de l'aperçu.

La vue **Conflits** recherche les copies `sync-conflict` dans le dossier du projet,
puis les compare au fichier courant. Un original supprimé peut aussi être examiné.
Les sauvegardes ordinaires continuent d'exclure ces copies ; la revue ne les supprime
pas. La résolution peut conserver la version courante, utiliser la copie de conflit
ou appliquer un texte fusionné manuellement, après aperçu et mise en pause du projet.

```sh
cysynck compare PROJECT_ID chemin/gauche.txt chemin/droite.txt
cysynck conflicts PROJECT_ID
cysynck conflict-diff PROJECT_ID notes.sync-conflict-20261104-120000-ABCDEFG.txt
```

Le CLI, l'API, la console Web et le desktop utilisent le même résultat JSON, avec `patch`
pour le texte exportable. Les patches sont vérifiés avec un vrai `git apply` ; avant
application sur un projet, les examiner et conserver une sauvegarde. Les règles Git
locales de conversion des fins de ligne peuvent modifier les octets produits.
Voir [la revue des différences](docs/diffs.md) pour la direction des patches et les limites.

La résolution conserve les deux versions dans l'état privé de CySynck et déplace les
fichiers sources dans `.cysynck-recovery/`, exclu des sauvegardes ordinaires et de Syncthing.
L'historique permet d'exporter ces copies vers un nouveau dossier. Un changement des
fichiers invalide l'aperçu ; un fichier apparu pendant l'opération n'est pas écrasé.
Une interruption est signalée dans la vue d'ensemble et doit être examinée avant
une autre résolution. CySynck laisse le projet en pause après application.

```sh
cysynck resolution-plan PROJECT_ID CONFLICT_PATH --choice conflict
cysynck pause PROJECT_ID
cysynck resolve-conflict PROJECT_ID CONFLICT_PATH --choice conflict --plan TOKEN
cysynck resolutions PROJECT_ID
cysynck resolution-export PROJECT_ID RESOLUTION_ID /srv/recovered-copies
```

Les choix binaires acceptent jusqu'à 100 Mio par fichier. La fusion manuelle est limitée
à 512 Kio de texte UTF-8 ; l'éditeur graphique utilise des fins de ligne LF.
Voir [la résolution et la récupération](docs/resolutions.md) pour les contraintes de stockage,
la reprise après interruption et les modes de synchronisation.

## VPN, tchat et envoi direct

Voir [la procédure réseau](docs/network.md). WireGuard reste le moteur du tunnel : CySynck
gère les identités, les profils et les outils officiels, sans réimplémenter sa cryptographie.
La découverte Syncthing donne des informations sur les appareils et transporte les profils publics.
Elle ne transforme pas un relais Syncthing en relais WireGuard.

L'activation du tunnel exige les droits système adaptés. Aucun tunnel ni route n'est créé
au simple démarrage de CySynck. Le canal direct écoute uniquement sur l'adresse VPN configurée.
La limite par fichier direct est de 1 Gio par défaut, réglable jusqu'à 4 Gio. Les quotas
de réception et d'envoi sont de 4 Gio chacun par défaut, réglables jusqu'à 64 Gio par profil :
`transfers limits`, puis `transfers configure --file-limit-mib 4096 --inbox-quota-mib 8192 --outbox-quota-mib 8192`.
Les deux pairs doivent accepter la taille choisie. Le mode ancien pair reste limité à 100 Mio.

La vue **Réseau → Ouvrir les fichiers reçus** permet de consulter la boîte de réception,
vérifier un fichier et exporter sa copie sans supprimer l'original reçu. Le Web et le CLI
offrent les mêmes opérations : `received list`, `received verify` et `received export`.
Voir [le guide de la boîte de réception](docs/inbox.md) (anglais).

## API, extensions et Unreal

Voir [l'architecture et le SDK](docs/architecture.md). Le SDK s'installe avec les binaires :

```cmake
find_package(CySynck CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE CySynck::Core)
```

`include/cysynck/c_api.h` expose une ABI C avec handles opaques et JSON UTF-8, utilisable
sans nlohmann/json côté appelant. Chaque chaîne renvoyée doit être libérée avec `cysynck_free`.
`integrations/unreal/CySynck` fournit un pont avec chargement dynamique et fonctions Blueprint.

## Suite du développement

- Installateurs natifs signés et mises à jour distribuées.
- Packaging natif signé, installeurs et validation effective sur macOS/Linux.
- Traversée NAT, rendez-vous et relais VPN optionnel pour les réseaux restrictifs.
- Stockage LFS personnalisé, verrous, protocole signé, compatibilité CyRevision et résolution graphique des conflits Git.
- Sauvegardes incrémentales par blocs à grande échelle et augmentation de la taille des transferts directs.

Les sources de Syncthing et CyRevision n'ont pas été incorporées. Les dépendances employées,
leurs versions et leurs licences sont décrites dans [THIRD_PARTY.md](THIRD_PARTY.md).

## Essais avec de vrais moteurs

Les tests ordinaires couvrent le cœur, l'API, Git, les extensions et le canal direct.
La suite `cysynck-diffs` applique les patches avec Git et vérifie les octets obtenus,
les fichiers vides, Unicode, les fins de ligne et les cas de conflits.
La suite `cysynck-resolutions` couvre les choix, la fusion manuelle, les copies et exports,
les aperçus périmés, les fichiers verrouillés et les créations concurrentes.
La suite `cysynck-backups` couvre la corruption, les manifests invalides, les aperçus périmés,
les références partagées entre projets et les sauvegardes concurrentes au nettoyage.
Pour activer les essais supplémentaires entre deux Syncthing locaux :

```sh
cmake -S . -B build -DCYSYNCK_TEST_SYNCTHING=/path/to/syncthing
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ils créent deux identités et des ports distincts, sans découverte Internet : texte,
binaire, Unicode, aller-retour, suppression, versionnement, pause/reprise, bundles Git,
modifications concurrentes hors connexion et revue des conflits réels,
propriété des processus et reprise après panne. Les données de test restent dans
`build/real-sync-data` pour permettre leur inspection.
