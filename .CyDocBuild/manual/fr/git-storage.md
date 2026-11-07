---
id: git-storage
title: Stockage des échanges Git
description: Nettoyer explicitement ses exports et les objets LFS d’échange sans référence.
section: Git
order: 43
parent: git-setup
sourceRevision: 1
---

# Stockage des échanges Git {#git-exchange-storage}

CySynck nettoie explicitement les exports Git du profil courant et les objets LFS inutilisés du dossier d’échange.
CLI, API C/C++, desktop et Web partagent aperçu/application. Ce nettoyage n’est jamais planifié automatiquement.

## Prévisualiser et appliquer {#preview-and-apply}

```sh
# Inspecter les exports locaux et objets LFS orphelins sans retrait.
cysynck git storage game-team

# Choisir les exports devenus inutiles aux pairs puis examiner les compteurs et erreurs.
cysynck git storage game-team --exports EXPORT_ID_1,EXPORT_ID_2

# Appliquer cette sélection exacte avec le jeton du plan renvoyé.
cysynck git storage game-team --exports EXPORT_ID_1,EXPORT_ID_2 --apply true --plan TOKEN
```

Sans `--exports`, aucun export n’est sélectionné : l’application retire seulement les objets LFS non référencés.
Sélectionner les exports et refaire l’aperçu avant application.
Retirer un export empêche les imports futurs par les autres pairs après propagation Syncthing.
Il n’existe ni accusé distant ni rétention distribuée.
POST authentifié `/api/projects/ID/git-storage` reçoit `{"exports":["EXPORT_ID"],"apply":false}`, puis la même sélection avec `apply:true` et `plan`.
Le résultat donne `exports`, `selected_exports`, `objects`, `stored_lfs_bytes`, `retained_objects`, `missing_objects`, `unused_objects`,
`reclaimable_bytes`, `ignored_entries`, `errors`, `can_prune`, `plan`. L’application ajoute `removed_exports`, `removed_objects`, `reclaimed_bytes`, `failures`.
Les compteurs décrivent le plan examiné ; les tailles logiques peuvent différer du disque réellement alloué sur un système compressé/dédupliqué.

## Propriété et cohérence {#ownership-and-consistency}

Seul `exchange/peers/LOCAL_PEER_ID` est éligible. Objets Git locaux, références, fichiers de travail, caches LFS approuvés et espaces des autres pairs ne sont pas retirés.
Les dépendances viennent de tous les manifests version 2 conservés ; un objet partagé reste protégé tant qu’un export local conservé le référence.
La version 1 n’a pas d’inventaire LFS transporté.
L’aperçu hashe les manifests et relève tailles/dates des catalogues et objets connus.
Sélection modifiée, nouvel export, manifest ou objet changé périment le jeton. Les bundles choisis sont entièrement hashés avant suppression.
Métadonnées conservées malformées, inconnues, liées ou incomplètes bloquent le nettoyage.
Limites : 10 000 manifests et 100 000 entrées de dossiers ; leur dépassement bloque aussi l’application.
L’application partage un verrou local avec la publication, dépublie chaque manifest avant son bundle puis rescane le catalogue avant les objets.
Les chemins SHA-256 minuscules ont une profondeur fixe ; aucune suppression récursive.
Fichiers inconnus/temporaires et bundles non indexés restent disponibles pour examen.
Les échecs partiels sont signalés ; un échec de retrait d’export arrête la collecte LFS.
Le verrou coordonne les écritures CySynck locales, pas Syncthing de manière distribuée. Chaque installation garde sa propre identité et n’écrit pas dans l’espace d’un autre pair.
