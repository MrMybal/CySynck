---
id: git-integration
title: Intégration Git en avance rapide après revue
description: Mettre à jour explicitement une branche propre ou sans premier commit et contrôler les reçus.
section: Git
order: 35
parent: git-setup
sourceRevision: 1
---

# Intégration Git en avance rapide après revue {#reviewed-git-branch-integration}

Une tête importée peut mettre à jour la branche locale, l’index et les fichiers de travail en avance rapide.
Les fichiers ordinaires indépendants et sorties de build ignorées peuvent rester depuis 0.20.
Une branche locale sans premier commit est acceptée. L’opération ne crée pas de commit de fusion, ne recule pas et ne fusionne pas les historiques divergents.
Pour ces derniers, la [fusion divergente séparée](doc:git-divergent-merge) peut produire un commit à deux parents après revue complète et choix explicites des conflits texte pris en charge.

## Prévisualiser et appliquer {#preview-and-apply}

Importer et examiner d’abord le bundle du pair, puis demander un nouvel aperçu :

```sh
cysynck git integrate game-team PEER_ID main
cysynck git integrate game-team PEER_ID main --apply true --plan PREVIEW_TOKEN
```

`--base COMMIT|unborn`, `--head FULL_OID`, `--local-ref refs/heads/BRANCH` facultatifs fixent le contexte.
Le jeton lie aussi projet, profil, digest de configuration, inventaires avant/après exacts et empreinte des métadonnées locales.
L’application réinspecte : références, index, fichiers ou configuration changés périment le jeton.
L’aperçu ne modifie ni index ni fichiers. CyUI/Web proposent **Prévisualiser l’intégration** puis **Appliquer l’avance rapide** en anglais/français.
La vue d’un commit historique garde son extraction séparée.
API C++ et ABI C version 1 :

```text
POST /api/projects/PROJECT_ID/git-integrate
{"peer":"PEER_ID","branch":"main"}
{"peer":"PEER_ID","branch":"main","apply":true,"plan":"PREVIEW_TOKEN"}
```

L’aperçu renvoie `can_integrate`, `errors`, `relation`, `base`, `head`, `local_ref`, `files_before`, `files_after`, `file_count`, `local_data`, `plan`.
`local_data` contient `files`, `directories`, `bytes`, `fingerprint` SHA-256 de l’inventaire de métadonnées trié.
Un aperçu bloqué donne les raisons sans jeton ; un contexte invalide peut renvoyer une erreur.
Succès : `applied:true`, `status:"complete"`, `receipt`, `receipt_path`.

## Périmètre actuel {#current-scope}

- Dépôt ordinaire exactement à la racine du projet, avec son dossier `.git` physique et une branche locale sélectionnée ; historique complet SHA-1/SHA-256.
- Index propre et octets de travail exacts. Les hashes bruts détectent les modifications cachées par le cache stat Git.
  Flags assume-unchanged/skip-worktree, bit exécutable changé et changements stagés entraînent un refus préservant les données.
- Fichiers non suivis/ignorés indépendants conservés. Une collision avec un fichier entrant ou parent nécessaire bloque même un fichier ignoré.
  Descendants locaux et dossiers vides bloquent aussi leur remplacement par un fichier importé.
  Alias de casse, liens/spéciaux et `.gitattributes` non suivi sont refusés. Aucun stash ni suppression de données locales.
- Au plus 100 000 entrées hors `.git` racine et 16 Mio de métadonnées ; les dossiers locaux vides comptent, les dossiers partagés avec l’arbre suivi ne font pas partie du sous-inventaire local.
- Fichiers réguliers seulement : 10 000 par arbre, 1 Gio par blob, 16 Gio par arbre.
  Liens, sous-modules, chemins dangereux, alias de métadonnées et collisions de casse refusés ; renommages changeant seulement la casse à gérer dans Git.
- Les pointeurs LFS nécessitent [la politique explicite `--lfs-policy pointers`](doc:git-lfs-integration) ; ils sont refusés par défaut.
  Les fichiers restent des pointeurs exacts à matérialiser séparément. Autres filtres et conversions texte/encodage/EOL/ident refusés.
  [L’extraction séparée](doc:git-extraction) reste disponible.
- Worktrees sparse/liés, index partagés, alternates, grafts, dépôts shallow/promisor et opérations Git actives nécessitent une gestion manuelle.
  Supprimer les redirections `GIT_*` de dépôt/configuration.

Git doit proposer `check-attr --source` (Git 2.43+).
Le `core.attributesFile` utilisateur effectif est désactivé ici ; attributs du dépôt et système doivent respecter les règles.
Après checkout, les octets sont de nouveau comparés aux identités Git examinées.

## Fraîcheur des données locales {#local-data-freshness}

L’inventaire local contient chemins relatifs, types, permissions, tailles et dates des fichiers réguliers, fichiers ignorés compris sans dépendre de leurs règles.
Son empreinte est liée au jeton et revérifiée après checkout ; ajout, retrait, renommage ou modification des métadonnées exige un nouvel aperçu.
Le reçu conserve compteurs, octets logiques et empreinte.
Les dates des dossiers sont omises car le checkout change leurs enfants suivis.
Les contenus locaux non suivis ne sont pas lus/hashés : un gros cache ignoré n’est pas relu.
Une réécriture de même taille avec date restaurée n’est pas détectée par cette empreinte, qui n’est pas une sauvegarde.
Les fichiers Git suivis gardent leurs contrôles d’octets. Arrêter les builds et écritures concurrentes ; un échec après checkout laisse un reçu incomplet sans annuler la mise à jour.

## Exécution et interruptions {#execution-and-interrupted-operations}

CySynck lance `git merge --ff-only` avec hooks, commandes de filtre, fsmonitor, sous-modules récursifs, autostash, maintenance et réseau désactivés.
Les options explicites neutralisent les préférences squash/autostash de branche.
Aucun helper de signature, éditeur, diff externe, outil de fusion ou réseau LFS. La configuration existante est conservée.
Git peut mettre à jour index, branche, reflogs et `ORIG_HEAD`.
Avant Git, un reçu privé `incomplete` est écrit dans `STATE/git-integrations/PROJECT_ID/ID.json`.
Le succès vérifie HEAD et chaque fichier puis note `complete`.
Commande échouée, erreur d’écriture ou interruption peuvent laisser des changements ; CySynck conserve cet état et le reçu sans reset, retour arrière ou nouvelle tentative automatique.
Examiner statut Git et reçu avant récupération avec Git, puis refaire un aperçu.
Le dossier exclusif `.git/cysynck-integration.lock` sérialise les intégrations CySynck entre profils.
Après arrêt brutal, inspecter le dépôt et confirmer qu’aucune intégration ne tourne avant de retirer ce dossier vide.
Les verrous Git existants sont conservés. Les autres clients ne reconnaissent pas ce verrou : les fermer et suspendre les écritures.
Les gardes ne transforment pas éditeurs/processus indépendants en transaction atomique.

## Historique et revue des reçus {#receipt-history-and-review}

**Reçus d’intégration Git** dans CyUI/Web liste l’historique.
Sélectionner **Vérifier le reçu d’intégration**, puis **Marquer comme examiné** pour une interruption passant les contrôles.
Cela enregistre la revue sans modifier statut `incomplete`, erreur ou dépôt.

```sh
cysynck git integrations PROJECT_ID --offset 0 --limit 20
cysynck git integration-check PROJECT_ID RECEIPT_ID
cysynck git integration-acknowledge PROJECT_ID RECEIPT_ID --plan CHECK_TOKEN
```

Les POST authentifiés correspondants reçoivent :

- `/api/projects/PROJECT_ID/git-integrations` : `{"offset":0,"limit":20}`.
- `/api/projects/PROJECT_ID/git-integration-check` : `{"receipt":"RECEIPT_ID"}`.
- `/api/projects/PROJECT_ID/git-integration-acknowledge` : `{"receipt":"RECEIPT_ID","plan":"CHECK_TOKEN"}`.

L’historique donne `records`, `errors`, `total`, `offset`, `limit`, `has_more`, `truncated`.
Les résumés omettent les arbres ; `needs_attention` repère les interruptions non examinées.
Au plus 50 reçus par page, tri date puis ID décroissants ; au plus 1 000 entrées et 64 Mio lus, 16 Mio par reçu.
`truncated` exige d’inspecter aussi le dossier d’état ; les entrées omises ne comptent pas dans `total`.
Les voisins invalides sont signalés sans masquer les valides.
Le contrôle retourne `position` (`before`, `after`, `other`, `unknown`), commit/branche courants, `tracked_matches`, `local_data_matches`, `lock_present`, `errors`, `can_acknowledge`, et `plan` si disponible.
Il dérive les deux arbres des commits complets et confronte les listes du reçu aux objets.
Les références de suivi peuvent avoir avancé/disparu, mais commits et blobs enregistrés doivent encore exister.
La vérification hors ligne sans hooks/filtres garde les restrictions de fichiers et ne rafraîchit pas l’index.
La revue n’est possible que pour un reçu incomplet non examiné au commit exact original/intégré, branche enregistrée, index/octets et métadonnées locales correspondants.
Les reçus 0.19 n’ont pas d’inventaire local : `local_data_matches:null`, annoncé dans l’interface ; les fichiers suivis restent vérifiés.
Métadonnées locales ou staging changés, autre branche/commit, objets absents, opération/verrou Git bloquent jusqu’à réconciliation manuelle.
Un reçu complet reste vérifiable après un commit légitime ultérieur, sans changer son ancien statut complet.
Le jeton lie reçu, branche/commit, index, configuration et inventaire courant. La revue recontrôle avant d’écrire `acknowledged_at` et les preuves.
Un contrôle changé ou déjà examiné ne rejoue pas son ancien jeton.
Les chemins viennent de l’état courant et d’un ID validé ; les champs stockés ne redirigent pas les lectures/écritures.
Ces preuves locales ne sont ni signées ni une sauvegarde du contenu non suivi.
Contrôle et revue ne retirent jamais verrous/fichiers, n’avancent pas HEAD et ne relancent/réinitialisent pas Git.
Suspendre aussi les écritures pendant ces contrôles ; le verrou ne couvre pas les processus externes.
Chaque helper Git est limité à 120 secondes. Avance rapide et reçu ne forment pas une transaction ; les commits enregistrés n’archivent pas le travail non commité.
L’exécution native Linux/macOS reste à valider sur les cibles.
Références : [avance rapide et fichiers ignorés](https://git-scm.com/docs/git-merge), [attributs d’un arbre](https://git-scm.com/docs/git-check-attr).
