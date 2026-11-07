# CySynck

[English](README.md) · Français

Un compagnon de synchronisation et de sauvegarde par projet en **C++20**, utilisant [Syncthing](https://github.com/syncthing/syncthing) et [CyUI](https://github.com/MrMybal/CyUI).
Un cœur commun sert le desktop natif Windows/macOS/Linux, le CLI, la console Web locale et le SDK. Les serveurs Linux se compilent sans dépendance graphique.

**Version 0.40.0 — développement actif.** Windows x64 est testé localement ; Linux/macOS natifs et le pont Unreal/Blueprint synchrone nécessitent une validation sur leurs cibles. Syncthing, Git et WireGuard s’installent séparément.

## Documentation

Le manuel **CyDoc** hors ligne comprend **l’anglais par défaut** et **le français**, des liens contextuels stables, la recherche, les favoris et les positions de lecture. Il s’ouvre directement en `file://`, sans serveur, CDN ni Internet.

- Paquet installé/build : ouvrir `Documentation/Documentation.html` avec son dossier `DocData/` complet.
- Desktop natif : **Aide (F1)** ouvre le sujet de la vue courante.
- CLI : `cysynck docs`, ou `cysynck docs git-materialization --language fr --print` pour obtenir l’URL locale sans navigateur.
- Web : l’aide contextuelle propose l’ouverture locale depuis un serveur en boucle locale et l’archive entière pour un poste distant. Extraire l’archive avant de lire le manuel.
- Dépôt : [sources anglaises du manuel](.CyDocBuild/manual/en/overview.md) · [sources françaises](.CyDocBuild/manual/fr/overview.md) · [régénération de la documentation](.CyDocBuild/README.md).

Le lecteur ne charge aucun module média facultatif ni Three.js. Les schémas sont compilés en SVG statiques. La référence C++/C/extensions provient des trois en-têtes publics distribués ; le lecteur ne publie pas les implémentations internes.

## Fonctions

| Domaine | Workflows disponibles |
| --- | --- |
| Projets et synchronisation | Bidirectionnel, envoi seul, réception seule, sauvegardes, échange Git et modes d’extensions natives de confiance |
| Desktop | Vue d’ensemble CyUI, différences/récupération, sauvegardes, Git/LFS, réseau, notification, démarrage par profil et panneau de commandes |
| Sauvegardes | Planification, exclusions après aperçu, rétention, stockage entier/blocs fixes/CDC, vérification et restauration sélective |
| Revue de fichiers | Différences côte à côte/unifiées bornées, patches Git, résolution explicite et copies de récupération |
| Collaboration Git | Initialisation, échanges bundle/LFS vérifiés, revue de branche/commit/trois versions, extraction et intégrations rapides/divergentes après revue |
| Fichiers LFS | Inspection/rechargement du cache, matérialisation sélective, contrôle/reprise d’interruption, restauration de pointeurs et export des assets préservés |
| Réseau | Appairage WireGuard, tchat direct, fichiers vérifiés avec reprise, quotas par profil et exports de réception |
| SDK et extensions | Cœur C++20, ABI C v1 JSON UTF-8, options typées et exemples C/C++/extensions compilables |
| Exploitation | Diagnostics, export systemd/LaunchAgent après revue et nettoyages explicites du stockage |

Le manuel donne les limites, étapes aperçu/application et procédures de récupération. La traversée NAT/relais de paquets WireGuard, la livraison différée du tchat, les installateurs/mises à jour signés et l’interopérabilité du protocole CyRevision ne sont pas implémentés. Les opérations multi-fichiers ne sont pas des transactions ; garder des sauvegardes indépendantes.

## Compiler et distribuer

Prérequis : **CMake 3.24+**, **compilateur C++20**, Git, **Node.js 22+** et npm. Node sert seulement à construire le manuel. CMake télécharge les dépendances natives ; l’archive CyDoc épinglée est incluse pour ne pas exiger l’accès à son dépôt privé.
Windows utilise Visual Studio 2022 C++ ; macOS les Xcode Command Line Tools. Le desktop Linux exige les bibliothèques de développement OpenGL/X11/Wayland de GLFW ; `libayatana-appindicator3-dev` active l’icône de notification.

```sh
npm ci --prefix .CyDocBuild
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
npm test --prefix .CyDocBuild
cmake --install build --config Release --prefix dist
cpack --config build/CPackConfig.cmake -C Release -B dist/packages
```

Ajouter `-DCYSYNCK_DESKTOP=OFF` pour un serveur sans interface. `scripts/build.ps1` et `scripts/build.sh` préparent les dépendances, compilent, testent et installent. Les paquets contiennent tout le manuel et `CySynck-Documentation.zip` ; seul le lecteur exporté est distribué, sans `.CyDocBuild/`.

Lancer `dist/bin/cysynck-desktop.exe` sous Windows, `dist/bin/cysynck-desktop` sous Linux ou `open dist/CySynck.app` sous macOS. L’archive Windows fournit aussi l’installateur utilisateur `Install-CySynck.ps1`. Garder tous les dossiers du paquet ensemble en cas de déplacement.

```sh
cysynck project add Notes /chemin/absolu/notes --id notes --mode backup --interval 60 --keep 30
cysynck backup notes
cysynck verify notes
cysynck docs quickstart --language fr
```

Choisir un dossier existant, citer les espaces, examiner le champ `ok` de la vérification et restaurer seulement dans un nouveau dossier séparé. `--state DIR` choisit un profil privé. La planification nécessite un desktop ou `cysynck serve` actif. `cysynck help` donne la syntaxe et `cysynck doctor --probe true` teste explicitement outils/connexion.

## Références de contribution

Les anciennes procédures de publication, packaging, compilation croisée et jalons sont conservées dans [.CyDocBuild/internal/legacy](.CyDocBuild/internal/legacy). Les petits fichiers `docs/*.md` maintiennent les anciens liens du dépôt et ne constituent plus une aide distribuée.
La CI native compile, teste et empaquette les cibles. Le workflow Gitleaks examine l’historique Git. Pour tester deux vrais pairs, configurer `-DCYSYNCK_TEST_SYNCTHING=/path/to/syncthing` avant compilation et CTest.

## Licence

CySynck est sous [MIT](LICENSE). Les notices des dépendances figurent dans [THIRD_PARTY.md](THIRD_PARTY.md) et les paquets. Syncthing, Git et WireGuard gardent leurs licences ; ni leurs sources/binaires ni les sources de CyRevision ne sont incorporés au projet.
