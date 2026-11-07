---
id: git-asset-export
title: Exporter les assets LFS préservés
description: Copier une ancienne version conservée en récupération sans remplacer le projet courant.
section: Git
order: 41
parent: git-materialization
sourceRevision: 1
---

# Exporter les assets LFS préservés {#exporting-preserved-lfs-assets-040}

La restauration des pointeurs conserve les assets développés inchangés dans `.cysynck-lfs/RECEIPT/before/`.
L’export les copie dans un nouveau dossier séparé, même après une mise à jour Git changeant l’identité LFS commitée.
Originaux, reçu source, fichiers de travail, HEAD, index et cache restent intacts.

```sh
cysynck git materialization-export PROJECT RECEIPT --destination /absolute/new/export
cysynck git materialization-export PROJECT RECEIPT --destination /absolute/new/export --apply true --plan TOKEN
```

Utiliser le jeton d’aperçu et répéter destination/sélection identiques.
`--paths-file selection.json` facultatif choisit des noms enregistrés exacts ou préfixes finissant par `/`, avec le tableau borné de la matérialisation.
La sélection vise les assets enregistrés pour préservation, pas tous les fichiers LFS de l’ancien commit.
CyUI/Web proposent aperçu/application de tous les assets préservés d’un reçu, en anglais/français.
POST authentifié `/api/projects/ID/git-materialization-export` reçoit `{"receipt":"ID","destination":"/absolute/new/export","paths":["Assets/"]}` ;
omettre `paths` pour tout exporter, ajouter `apply:true`/`plan` pour appliquer. La même route JSON C/C++ ne change pas l’ABI.

## Vérification et destination {#verification-and-destination}

Seuls les reçus schéma 2 de restauration des pointeurs sont acceptés.
Identités et sélection sont vérifiées contre le commit Git immuable original.
Les fichiers préservés sélectionnés doivent garder SHA-256, taille, date et permissions enregistrés.
Sources absentes/modifiées bloquent l’export ; une sélection explicite peut omettre un asset indisponible.
Entrées inconnues ou liens dans la récupération exigent un examen manuel.
L’aperçu renvoie `files`, `bytes`, `excluded`, `selection`, `errors` par chemin, `can_export`, `plan`, sans créer de destination.
Le jeton lie profil/projet, projets enregistrés, octets du reçu, empreintes source, sélection et destination.
HEAD peut avoir avancé : l’opération exporte l’ancienne version enregistrée.
La destination doit être absolue, absente, avec parent ordinaire existant, séparée de tous les projets, échanges et profil.
Fichiers/dossiers existants, liens et jonctions sont refusés.
Le verrou Git commun sérialise l’application ; les verrous préexistants sont conservés pour examen.
La destination est réservée exclusivement. Les assets sont copiés en flux et flush sous `files/PATH`, vérifiés puis reçoivent permissions/date de la source.
Prévoir l’espace des copies développées choisies, au plus 4 Gio par asset et 16 Gio au total.
Aucun helper LFS, filtre de checkout, réseau ou cache local.
`manifest.json` note provenance, hashes, sélection, progression et statut.
Succès : `exported:true`, `status:"complete"`, `files_written`.
Une erreur après création du manifest garde un manifest `incomplete`, les copies partielles et les sources ; les examiner puis choisir une nouvelle destination pour retenter.
Aucun nettoyage, écrasement, reprise ou remplacement automatique des fichiers de travail.
Le commit original doit encore être lisible dans le dépôt. Suspendre les écritures externes : le verrou CySynck ne verrouille pas les autres logiciels.
Les essais natifs Linux/macOS, de coupure, de gros assets et des nouvelles interactions GUI restent à effectuer sur leurs cibles.
