---
id: git-extraction
title: Extraire une révision Git importée
description: Copier une révision et ses assets vérifiés dans un nouveau dossier séparé.
section: Git
order: 38
parent: git-setup
sourceRevision: 1
---

# Extraire une révision Git importée {#extract-an-imported-git-revision}

CySynck extrait l’arbre complet d’une branche importée, ou d’un commit accessible choisi, dans un nouveau dossier séparé.
Le résultat est une copie des fichiers ; métadonnées Git et historique restent dans le dépôt source.
[L’avance rapide](doc:git-integration) et [la fusion divergente éligible](doc:git-divergent-merge) sont séparées ; les rebases relèvent de Git.
L’extraction fonctionne avant le premier commit local et préserve HEAD, index, fichiers modifiés ou stagés.

## Prévisualiser puis extraire {#preview-then-extract}

```sh
cysynck git extract PROJECT PEER BRANCH /absolute/new-output
# Conserver les options et copier le plan renvoyé :
cysynck git extract PROJECT PEER BRANCH /absolute/new-output --apply true --plan TOKEN
# Choisir un commit historique ou conserver explicitement les pointeurs LFS bruts :
cysynck git extract PROJECT PEER BRANCH /absolute/another-output --commit FULL_COMMIT_ID --lfs false
```

Par défaut, la révision est la tête importée. Un commit doit être un identifiant SHA-1/SHA-256 complet accessible depuis cette branche ; abréviations et expressions sont refusées.
L’aperçu donne `commit`, `files`, `file_count`, `bytes` développés, `lfs_files`, `lfs_bytes`, `errors`, `can_extract`, `plan`.
Un inventaire bloqué n’a pas de plan. La destination reste absente ; elle ne peut chevaucher aucun projet, échange ou profil et une destination existante même vide est conservée.
Le jeton lie projet, références locales/importées, commit choisi, inventaire exact, destination et politique/cache LFS. Un changement le périme.
`base`, `head`, `local_ref` facultatifs fixent le contexte de revue ; `--base unborn` vaut commit local null. Les interfaces les fixent automatiquement.
Dans CyUI/Web, examiner la branche, choisir éventuellement un commit historique puis **Extraire la révision Git**.
Choisir nouveau dossier/politique LFS, préparer, lire les erreurs et **Extraire vers un nouveau dossier**.
Le commit affiché identifie l’arbre complet copié, indépendamment du parent choisi pour son diff.

## Octets, LFS et limites {#bytes-lfs-and-limits}

Les fichiers réguliers conservent les octets commités : binaires, chemins UTF-8 et fichiers vides compris.
Le bit exécutable devient `0755` ou `0644` sur POSIX.
Aucune conversion EOL/encodage, filtre de checkout, hook, attribut d’archive ou commande de sous-module.
Les [arbres](https://git-scm.com/docs/git-ls-tree) et [blobs](https://git-scm.com/docs/git-cat-file) sont lus directement et les identités vérifiées.
Les pointeurs canoniques LFS v1 se développent par défaut depuis le cache local vérifié, y compris un `lfs.storage` correspondant explicitement approuvé.
Les assets sont copiés en flux avec contrôle taille/SHA-256, sans téléchargement.
Objet absent, lié, incohérent ou corrompu bloque le développement.
`--lfs false` copie les pointeurs bruts sans cache ; cette politique est indépendante de l’export automatique du projet.

- Au plus 10 000 fichiers et 4 096 assets LFS uniques.
- Blob Git ordinaire : 1 Gio depuis 0.18 (4 Mio en 0.17), par buffers bornés.
- Asset LFS : 4 Gio ; sortie développée totale : 16 Gio.
- Liens et sous-modules nécessitent un traitement séparé.
- Chemins relatifs UTF-8 sûrs sur la cible ; métadonnées de dépôt, sorties de racine, alias de périphérique/flux Windows et collisions sont refusés.
  Les variations de casse ASCII de fichiers/dossiers sont refusées partout ; Windows utilise aussi sa comparaison Unicode ordinale.
  La création exclusive protège contre les autres alias rencontrés pendant application.
- Historique complet non shallow, sans clone partiel/promisor ; supprimer les redirections `GIT_*` propres au dépôt.

L’aperçu vérifie disponibilité/intégrité LFS. Les blobs ordinaires sont lus en blocs stdout d’au plus 16 Kio,
avec taille examinée imposée avant écriture et SHA-256 calculé en flux.
Après fermeture, [hash-object --no-filters](https://git-scm.com/docs/git-hash-object) contrôle l’identité exacte sans écrire d’objet ni lancer de filtre.
La sortie complète est relue pour SHA-256. Une corruption peut donc interrompre l’application après un aperçu de métadonnées réussi.
Chaque commande Git est limitée à 120 secondes ; une erreur préserve sortie partielle et reçu incomplet.

## Reçus et copies interrompues {#receipts-and-interrupted-copies}

La destination et chaque fichier sont réservés exclusivement. Un reçu JSON privé est écrit avant copie dans `STATE/git-extractions/PROJECT/ID.json`.
Le succès note `status:"complete"`, la révision fixée, l’inventaire, les SHA-256 par fichier et `files_written` ; la réponse donne `receipt` et `receipt_path` absolu.
Une exception conserve les fichiers partiels et note `status:"incomplete"` avec l’erreur, la destination et l’identifiant du reçu.
Un arrêt brutal laisse aussi le reçu incomplet ; hashes et compteurs peuvent être en retard sur les sorties jusqu’à sauvegarde du reçu.
Aucun nettoyage récursif, retour arrière ou reprise automatique. Examiner la copie puis choisir une autre nouvelle destination pour retenter.
Suspendre les écritures externes ; les références source sont revérifiées à la fin.

## API et SDK {#api-and-sdk}

POST authentifié `/api/projects/ID/git-extract` accepte :

```json
{"peer":"PEER","branch":"main","destination":"/absolute/new-output","lfs":true}
```

`commit` facultatif choisit une révision accessible. L’application ajoute `apply:true` et `plan` sans changer les autres entrées.
Le SDK C++ et l’ABI C JSON UTF-8 exposent la même route sans changer de version d’ABI.
