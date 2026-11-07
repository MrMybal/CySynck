---
id: git-lfs
title: Échange Git et Git LFS hors ligne
description: Transporter bundles et objets LFS vérifiés dans un dossier Syncthing séparé.
section: Git
order: 31
parent: git-setup
sourceRevision: 1
---

# Échange Git et Git LFS hors ligne {#offline-git-and-git-lfs-exchange}

Chaque appareil conserve son dépôt Git. Syncthing transporte un dossier d’échange de bundles, manifests SHA-256 et objets LFS ; un `.git` actif n’est jamais synchronisé.
L’export/import n’exécute ni `git push`, ni contact de serveur LFS, ni checkout, ni fusion. Git reste externe ; l’inventaire et le transport d’objets sont en C++20.

```sh
cysynck project add Game /work/game --id game-team --mode git-bundles --exchange-path /work/game-exchange
cysynck git export game-team
cysynck git inbox game-team
cysynck git import game-team PEER_ID BUNDLE_ID
```

L’export et la synchronisation du mode Git incluent LFS par défaut depuis 0.13. Les interfaces natives/Web proposent la même option en anglais/français.
L’export exige une branche attachée commitée. Le contenu non commité reste local.

## Contenu exporté {#what-is-exported}

Le bundle contient la branche attachée et son historique accessible. L’inventaire LFS examine les petits blobs de cet historique exact : ancêtres fusionnés, versions anciennes et assets supprimés compris.
Les contenus répétés apparaissent une fois par SHA-256 ; les objets accessibles seulement par une branche indépendante sont exclus.
Le commit du manifest provient du bundle terminé : déplacer la branche pendant l’export ne change pas sa tête déclarée ensuite.
Les pointeurs LFS v1 acceptés sont canoniques : URL standard de version, `oid sha256`, taille non négative, fins LF, moins de 1 024 octets.
Extensions et anciens pointeurs Hawser échouent explicitement. Un fichier ordinaire vide ne nécessite aucun objet LFS.
Références : [spécification LFS](https://github.com/git-lfs/git-lfs/blob/main/docs/spec.md), [interface batch de Git](https://git-scm.com/docs/git-cat-file).
Les objets locaux viennent de `<git-common-dir>/lfs/objects/aa/bb/SHA256` ; les worktrees liés partagent ce cache.
Git LFS n’est pas nécessaire à l’export/import ni à la [matérialisation explicite](doc:git-materialization) de pointeurs déjà présents ; l’installer pour suivre de nouveaux assets et les développer avec Git dans le workflow ordinaire.
Une redirection `lfs.storage` exige l’approbation explicite du projet. Les dépôts shallow sont refusés ; aucun objet promisor absent n’est téléchargé.

## Politique du projet et caches personnalisés {#project-policy-and-custom-caches}

`git_lfs`, booléen activé par défaut, sert aux exports manuels et à la synchronisation. L’argument `lfs` explicite remplace cette politique pour une opération.
CLI, Paramètres natifs et Web exposent ces valeurs persistantes :

```sh
cysynck project set game-team --git-lfs true
git -C /work/game config lfs.storage /work/game-lfs
cysynck project set game-team --lfs-storage /work/game-lfs
```

Créer d’abord la racine de stockage. `git_lfs_storage` est son chemin absolu ; CySynck utilise `objects/aa/bb/SHA256` dessous.
Ce dossier doit être séparé des racines enregistrées, échanges et état. Des projets explicitement configurés peuvent le partager ; CySynck ne nettoie jamais ce cache.
Les liens/jonctions sont refusés ; les futurs projets ne peuvent pas chevaucher un cache approuvé.
Le `lfs.storage` Git effectif doit résoudre au même dossier. Les valeurs relatives se résolvent contre le dossier Git commun, worktrees liés compris, selon [la configuration Git LFS](https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-config.adoc).
CySynck ne change pas Git. Déplacer le cache, le supprimer ou le remplacer par un lien invalide l’approbation.
Vider `git_lfs_storage` après retrait de la redirection Git rétablit le cache par défaut. Les anciens profils sans ces paramètres gardent LFS actif et le cache Git normal.

## Publication, réception et import {#publication-reception-and-import}

Les exports version 2 publient, sous l’espace du pair choisi :

```text
peers/PEER/BUNDLE.bundle
peers/PEER/BUNDLE.lfs.json
peers/PEER/lfs/objects/aa/bb/SHA256
```

Les objets sont copiés/vérifiés avant le manifest final. La publication utilise un lien physique sans remplacement dans le système de fichiers cible, qui doit le permettre (NTFS, ext4 ou APFS par exemple).
Les objets existants identiques sont vérifiés/réutilisés ; un objet endommagé est conservé et fait échouer l’opération.
Les chemins sont dérivés d’identifiants validés ; liens/jonctions sont refusés.
Syncthing peut livrer dans n’importe quel ordre. `lfs_status` de la boîte d’import donne nombre, octets, absents et `complete` selon présence/taille ; l’import vérifie entièrement SHA-256.
Les interfaces désactivent l’import tant que des objets manquent.
L’import vérifie le bundle figé et tous les objets reçus. [git bundle unbundle](https://git-scm.com/docs/git-bundle) ajoute les objets immuables sans publier de référence, puis l’inventaire LFS est recalculé à partir du commit importé.
Omissions, ajouts, doublons et incohérences du manifest sont refusés.
Les objets LFS vérifiés sont installés dans le cache avant `refs/remotes/cysynck/PEER/BRANCH`. Hooks et récupération récursive de sous-modules sont désactivés.
HEAD, index et fichiers de travail restent inchangés. Revue et intégration sont séparées.
Un import interrompu/refusé peut laisser des objets Git/LFS réutilisables sans publier la référence avant vérification complète.
Un export interrompu peut laisser des objets sans référence ou un bundle non publié.
Aucune expiration, coordination distante ou confirmation automatique. Limiter les échanges aux pairs de confiance.
Le [nettoyage Git explicite](doc:git-storage) retire après aperçu des exports locaux choisis et objets d’échange sans référence ; bundles non indexés/fichiers inconnus sont conservés et le cache local du dépôt n’est jamais nettoyé.

## Compatibilité et limites {#compatibility-and-limits}

Les deux appareils nécessitent 0.13+ pour LFS. Les anciens clients ignorent `.lfs.json`, évitant l’import d’un bundle sans ses assets.
Les manifests version 1 restent lisibles. Pour produire explicitement un bundle historique ne transportant que les pointeurs :

```sh
cysynck git export game-team --lfs false
```

Ce mode transporte objets Git et pointeurs, pas les assets LFS. La case d’export native/Web choisit cette politique par opération ; la synchronisation automatique du mode utilise la valeur enregistrée.
Prévoir un workflow compatible avec les anciens pairs.

- 100 000 objets Git accessibles inspectés par export/import.
- 4 096 objets LFS uniques, 4 Gio par objet, 16 Gio par bundle.
- Manifest de 1 Mio, lots bornés et copies en flux ; aucun asset entier en mémoire. Ces limites ne plafonnent pas tous les exports conservés ni tout le cache.
- Objets Git SHA-1/SHA-256 ; contenus LFS toujours SHA-256.
- Les objets LFS des sous-modules ne sont pas exportés depuis le parent.
- Signatures, verrous LFS, échanges avec un serveur LFS et interopérabilité du protocole CyRevision restent à réaliser.

## API et SDK {#api-and-sdk}

Les routes authentifiées sont disponibles par SDK C/C++ et CLI distant :

| Route POST | Corps/résultat |
| --- | --- |
| `/api/projects/ID/export` | `{"lfs":true}` par défaut, ou `{"lfs":false}` historique |
| `/api/projects/ID/inbox` | `{}` ; manifests avec `lfs_status` en version 2 |
| `/api/projects/ID/import` | `{"peer":"PEER","bundle":"BUNDLE"}` ; compteurs LFS vérifiés au succès |

Le champ `lfs` version 2 contient `version:1`, `scope:"branch-history"`, `objects:[{"sha256":"...","size":N}]` uniques/triés et leur total `bytes`.
Il ne contient ni chemin local, ni secret, ni nom de fichier non commité. La version 1 ne transporte pas cet inventaire.
[L’inspection/rechargement du cache](doc:git-cache) récupère des objets absents disponibles dans un échange existant sans importer de référence ni checkout.
[L’avance rapide avec pointeurs](doc:git-lfs-integration) effectue explicitement un checkout des pointeurs canoniques, premier import compris ; leur matérialisation reste séparée.
