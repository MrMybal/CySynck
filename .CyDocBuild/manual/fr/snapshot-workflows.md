---
id: snapshot-workflows
title: Parcourir, comparer et restaurer les snapshots
description: Retrouver des chemins sauvegardés et restaurer seulement les données choisies.
section: Sauvegardes
order: 13
parent: backups
sourceRevision: 1
---

# Parcourir, comparer et restaurer les snapshots {#snapshot-browsing-comparison-and-selective-restoration}

Les trois workflows sont disponibles depuis 0.29 dans le cœur C++20, le CLI, le SDK C/C++ et les interfaces CyUI/Web bilingues.
Ouvrir **Sauvegardes** dans le desktop ou la section du projet dans le Web.

## Parcourir les fichiers sauvegardés {#browse-saved-files}

Sélectionner un snapshot et **Parcourir les fichiers sauvegardés**. Le préfixe facultatif d’un dossier sélectionne ses descendants sans les voisins de nom similaire.
Les pages contiennent au plus 200 fichiers réguliers (100 par défaut), dans l’ordre exact des chemins.
Tailles, SHA-256 enregistrés, permissions et formats viennent du manifest validé. Le parcours ne hashe pas les contenus et ne parcourt pas le projet courant ; les métadonnées restent consultables malgré un objet absent/corrompu.

```sh
cysynck snapshot-files PROJECT SNAPSHOT --prefix Content --limit 100 --offset 0
cysynck snapshot-files PROJECT SNAPSHOT --prefix Content --offset 100 --manifest-hash HASH
```

Le résultat contient `files`, `total`, `bytes`, `offset`, `limit`, `has_more`, le `prefix` normalisé et `manifest_hash`.
Les totaux concernent tout le préfixe. `content_verified:false` distingue les métadonnées d’un contrôle d’intégrité.
Fournir `manifest_hash` aux pages suivantes pour refuser un manifest modifié. La rétention peut retirer une ancienne version ; recharger la liste des snapshots.
Seul le manifest nommé est lu. Limites : manifest 64 Mio, page 2 Mio, offset 1 000 000, préfixe 4 096 octets UTF-8.

## Restaurer une sélection de fichiers ou dossiers {#restore-selected-files-or-folders}

Cliquer sur des chemins sauvegardés ou saisir un chemin relatif par ligne dans **Sélection de restauration**.
Un dossier finit par `/` : `Content/` vise ses descendants, `Content` un fichier exact.
Les chemins distinguent la casse et utilisent `/`. Les jokers restent littéraux. Chemins absolus, antislashs, composants `.`/`..` et sélecteurs absents sont refusés.
Les chevauchements sont dédupliqués. Limites : 256 sélecteurs, 4 096 octets UTF-8 chacun, 16 Kio cumulés. Les noms avec retour à la ligne ne se sélectionnent pas individuellement ; la restauration entière reste possible.
Une sélection vide dans l’interface restaure tout. L’API omet `paths` ; un tableau explicitement vide est refusé.
Le CLI lit un tableau JSON UTF-8, par exemple `["Content/","Source/Game.cpp"]` :

```sh
cysynck restore-plan PROJECT SNAPSHOT /new/copy --paths-file selection.json
cysynck restore PROJECT SNAPSHOT /new/copy --paths-file selection.json --plan TOKEN
```

L’aperçu vérifie seulement les contenus sélectionnés, entiers/fixes/CDC. Une corruption non sélectionnée ne bloque pas les données saines choisies.
Le manifest complet est toujours validé pour les chemins et recettes. `manifest_hash` facultatif fixe la version parcourue.
Le `plan` lie profil, projet/racine, manifest complet, sélection normalisée et destination. La restauration partielle exige ce jeton et refait les vérifications ; tout changement nécessite un nouvel aperçu.
La restauration entière reste compatible avec les anciens appels sans jeton ; en fournir un ajoute le même contrôle de fraîcheur.
La sortie est un **nouveau** dossier séparé, conservant les chemins relatifs et ne chevauchant aucun projet, échange ou profil.
Les liens et destinations existantes sont refusés. Les fichiers sont créés exclusivement et leurs hashes finaux vérifiés, avec les permissions habituelles.
Une erreur disque, une écriture externe ou un arrêt peut laisser une sortie partielle, sans retour arrière ni reprise automatique. La source est conservée. Le jeton n’est ni une signature ni un verrou du système de fichiers.

## Comparer deux versions sauvegardées {#compare-two-saved-versions}

Choisir les snapshots initial/final puis **Comparer les sauvegardes**. La liste paginée signale ajouts, suppressions, modifications et changements de permissions selon hashes, tailles et permissions enregistrés.
Un changement de politique de stockage seul n’est pas un changement logique. Les fichiers de travail, Git et le moteur de synchronisation ne participent pas.

```sh
cysynck snapshot-compare PROJECT OLD_SNAPSHOT NEW_SNAPSHOT --limit 100
cysynck snapshot-compare-file PROJECT OLD_SNAPSHOT NEW_SNAPSHOT Source/Game.cpp
```

Les réponses donnent `left_hash`/`right_hash` ; les fournir aux pages et requêtes de fichiers suivantes. Les interfaces le font et ignorent les réponses périmées.
Les petits aperçus lisent et vérifient les deux versions, y compris fichiers ajoutés, supprimés ou vides, avec visualisation CyUI et patch compatible Git.
Limites texte : 512 Kio et 10 000 lignes par côté. Les données binaires/non UTF-8 n’ont pas de patch.
Les gros fichiers exposent leurs métadonnées et la validation du format sans lire tous les contenus : `identical:null` ne garantit pas l’égalité.
Utiliser vérification ou restauration pour l’intégrité complète. Les permissions sont signalées séparément du patch texte.

## API et SDK {#api-and-sdk}

Toutes les routes passent par POST authentifié et l’ABI C version 1, sous `/api/projects/PROJECT/` :

| Route | Corps |
| --- | --- |
| `snapshot-files` | `{"snapshot":"ID","prefix":"Content/","offset":0,"limit":100}` |
| `restore-plan` | `{"snapshot":"ID","paths":["Content/"],"destination":"/new/copy"}` |
| `restore` | Même corps avec `"plan":"TOKEN"` |
| `snapshot-compare` | `{"left":"OLD","right":"NEW","offset":0,"limit":100}` |
| `snapshot-compare-file` | `{"left":"OLD","right":"NEW","path":"Source/Game.cpp"}` |

Les champs facultatifs `manifest_hash` ou `left_hash`/`right_hash` fixent les manifests.
Le CLI accepte `--manifest-hash`, `--left-hash`, `--right-hash` et aussi `--prefix` pour les listes de comparaison.
