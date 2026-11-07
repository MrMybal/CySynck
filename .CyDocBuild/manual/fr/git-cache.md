---
id: git-cache
title: Inspecter et recharger le cache LFS hors ligne
description: Vérifier les objets référencés et récupérer ceux disponibles dans un échange existant.
section: Git
order: 42
parent: git-setup
sourceRevision: 1
---

# Inspecter et recharger le cache LFS hors ligne {#offline-git-lfs-cache-inspection-and-refill-032}

La vue native **Cache LFS** et le panneau Git Web inspectent les objets référencés par l’historique accessible d’une branche commitée.
Ils rechargent les objets immuables absents depuis le dossier d’échange Syncthing d’un pair explicitement choisi.
Ces opérations ne contactent aucun serveur LFS, n’exécutent aucun filtre, ne changent ni configuration, ni checkout, ni références/index Git.
CLI et API C/C++ JSON partagent ces fonctions avec l’ABI C version 1.

## Inspecter et vérifier {#inspect-and-verify}

```sh
cysynck git lfs-cache PROJECT
cysynck git lfs-cache PROJECT --verify true
cysynck git lfs-cache PROJECT --peer IMPORTED_PEER --branch main --offset 100 --limit 100
```

POST `/api/projects/ID/git-lfs-cache` accepte `peer`/`branch` ensemble pour viser `refs/remotes/cysynck/PEER/BRANCH`.
Sinon HEAD local, même détaché, est utilisé. Une branche sans premier commit exige de choisir une branche importée.
Tout l’historique accessible est inclus, assets anciens/supprimés compris ; le contenu répété compte une fois par SHA-256.
C’est un inventaire d’objets, pas une liste de noms de fichiers courants.
L’inspection par défaut observe présence, taille, ticks de date et permissions.
`verify:true` lit et hashe chaque objet présent de bonne taille.
États : `missing`, `present` non vérifié, `verified`, `corrupt`, `size_mismatch`, `unsafe`.
Absents et chemins endommagés sont séparés. `verification_requested` rappelle la politique ; `content_verified` est vrai seulement si la vérification a été demandée et aucun objet ne manque/n’est endommagé.
Le résultat contient révision/contexte fixés, cache approuvé, compteurs/octets, pairs sources, `inventory_hash` et une page d’`objects`.
`offset` va de 0 à 4 096, `limit` de 1 à 200 (100 par défaut). Fournir le premier `inventory_hash` aux pages suivantes en gardant vérification/révision identiques.
Chaque page reconstruit l’inventaire entier borné, hashes complets compris si demandés ; changements locaux, révision ou configuration le périment.
Limites : 100 000 objets Git accessibles, 4 096 assets distincts, 4 Gio par asset et 16 Gio au total.
Parcourir l’historique ou hasher un gros cache peut prendre du temps.
SHA-1/SHA-256, pointeurs canoniques et `lfs.storage` explicitement approuvé suivent [les règles LFS](doc:git-lfs).
Aucun téléchargement implicite ni nettoyage du cache.

## Recharger seulement les objets absents disponibles {#refill-only-available-missing-objects}

```sh
cysynck git lfs-recover PROJECT SOURCE_PEER
cysynck git lfs-recover PROJECT SOURCE_PEER --apply true --plan PREVIEW_TOKEN
# Sous-ensemble exact facultatif : tableau JSON des identités LFS SHA-256 commitées.
cysynck git lfs-recover PROJECT SOURCE_PEER --objects objects.json
```

POST `/api/projects/ID/git-lfs-recover` reçoit `source_peer`, révision `peer`/`branch` facultative et `objects` facultatif non vide (au plus 4 096 identités uniques commitées).
Identités inconnues ou dupliquées sont refusées.
Les sources viennent uniquement de `EXCHANGE/peers/SOURCE_PEER/lfs/objects/aa/bb/SHA256` ; le catalogue examine au plus 1 000 entrées de pairs.
Le manifest original n’a pas besoin d’être conservé : taille/SHA-256 complets doivent correspondre à une identité de l’historique local choisi.
L’aperçu lit/vérifie les candidats sans créer d’objet de cache ni de reçu.
Il liste objets éligibles, `unavailable`, objets existants conservés et octets récupérables.
Seuls les objets locaux absents avec source vérifiée sont éligibles. Toute entrée existante reste, même de taille erronée, corrompue ou dangereuse.
Un sous-ensemble peut être récupérable : les interfaces montrent compteurs/octets et indisponibles avant application.
L’aperçu JSON est limité à 8 Mio ; il ne promet pas de tout récupérer ni de remplacer les objets locaux endommagés.
Ajouter `apply:true`/`plan` et répéter source, révision et sélection identiques.
Le jeton lie profil/projet, contexte/configuration Git, cache, identités/métadonnées source et sélection.
Un nouvel examen et un verrou exclusif du cache précèdent les écritures.
Chaque copie est revérifiée et publiée par lien physique sans remplacement ; un objet concurrent inattendu n’est jamais écrasé.
Le système de fichiers doit permettre les liens physiques. Les ajouts immuables déjà copiés ne sont pas annulés en cas d’échec.
Les observations de métadonnées ne figent pas les écritures externes et ne détectent pas toute modification conservant ces valeurs ; SHA-256 contrôle indépendamment les sources/copies.
Les autres logiciels doivent respecter le verrou pour éviter les courses. Liens/jonctions des racines, caches et échanges sont refusés.
L’API n’isole pas un processus local malveillant.

## Reçus locaux {#local-receipts}

```sh
cysynck git lfs-recoveries PROJECT
cysynck git lfs-recovery PROJECT RECEIPT_ID
```

POST `git-lfs-recoveries` reçoit `offset`/`limit` (20 par défaut, 100 au plus), avec résumés et diagnostics de voisins malformés.
POST `git-lfs-recovery` reçoit `receipt` pour les détails.
Les reçus privés `STATE/git-lfs-recoveries/PROJECT/ID.json` contiennent identités projet/profil, date, pair source, objets planifiés/installés et statut complet/incomplet.
Le catalogue est limité à 10 000 entrées. Un reçu durable incomplet précède la copie puis est finalisé au succès/échec.
Un arrêt peut laisser des ajouts valides pas encore notés dans `installed` : réinspecter le cache après interruption.
Le statut décrit le résultat alors observé, pas une vérification ultérieure. Aucune reprise, expiration, suppression distante ni annulation automatique.
CyUI/Web exposent inventaire, vérification, rechargement explicite par pair et historique paginé en anglais/français.

## Vérifier et examiner les reçus de récupération {#checking-and-reviewing-recovery-receipts-033}

```sh
cysynck git lfs-recovery-check PROJECT RECEIPT_ID
cysynck git lfs-recovery-acknowledge PROJECT RECEIPT_ID --plan CHECK_TOKEN
```

POST `git-lfs-recovery-check` reçoit `{"receipt":"ID"}` et vérifie tous les objets planifiés, même les copies valides absentes de la liste `installed` d’un reçu interrompu.
Les identités taille/SHA-256 doivent appartenir à l’historique commité enregistré, encore présent localement.
HEAD ou la référence de suivi peuvent avoir avancé sans invalider ce contrôle d’historique.
Aucun pair d’échange ni réseau nécessaire. Le cache actuellement approuvé doit correspondre à l’ancien emplacement ; un déplacement produit un diagnostic plutôt qu’une lecture arbitraire du chemin du reçu.
Le résultat contient `record`, `receipt_hash`, `revision`, `store`, `objects`, `verified`, `missing`, `damaged`, `errors`, `position`, `content_verified`, `can_acknowledge`, `plan`.
Positions : `complete` si tous les octets planifiés sont vérifiés, `partial`, `missing`, `damaged`, `unknown` si configuration/historique/reçu invalide.
La vérification ne réécrit pas le statut original, ne crée pas d’objet et ne change ni cache, références/index, fichiers de travail ou reçu.
POST `git-lfs-recovery-acknowledge` exige `receipt` et le dernier `plan`.
Il recalcule les hashes et lie profil, paramètres du projet, reçu, configuration, cache approuvé et métadonnées observées.
Seuls les reçus incomplets non examinés sans objet endommagé ni erreur sont éligibles.
Un cache partiel ou totalement absent peut être explicitement examiné ; les manquants restent visibles et à récupérer.
La revue ajoute date et compteurs vérifiés/absents, mais conserve `status`, `applied`, `installed`, erreur et octets : elle ne réessaie pas et ne déclare pas un succès de copie.
L’historique expose `acknowledged`, `acknowledged_at`, `needs_attention`.
Ce sont des observations, pas un snapshot atomique ; des écritures ultérieures imposent un nouveau contrôle.
Entrées remplacées/dangereuses restent préservées. Les reçus sont des preuves locales privées, pas des attestations signées.
CyUI/Web fournissent détails/contrôle/revue en anglais/français ; CLI et SDK gardent la même API et l’ABI C version 1.
