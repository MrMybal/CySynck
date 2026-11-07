---
id: git-materialization
title: Matérialiser les assets LFS hors ligne
description: Développer des pointeurs commités depuis le cache vérifié et reprendre une publication interrompue.
section: Git
order: 39
parent: git-setup
sourceRevision: 1
---

# Matérialiser les assets LFS hors ligne {#offline-lfs-working-tree-materialization}

Après checkout d’une révision contenant des pointeurs LFS, CySynck peut remplacer ces pointeurs exacts par des assets déjà présents dans le cache local vérifié.
Cette action est explicite et séparée de l’import, l’intégration et l’extraction. Aucun téléchargement ni exécutable Git LFS ; Git reste responsable du checkout et des fusions.
[L’avance rapide explicite](doc:git-lfs-integration) peut laisser des pointeurs prêts pour cette opération.
Dans Échange Git, choisir **Prévisualiser la matérialisation LFS**, examiner les états puis **Matérialiser les assets vérifiés**. CyUI/Web sont anglais/français.
Le CLI et l’ABI C version 1 accèdent au même cœur C++20 :

```sh
cysynck git materialize PROJECT_ID
cysynck git materialize PROJECT_ID --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/ID/git-materialize` reçoit `{}` pour l’aperçu, puis `apply:true` et son `plan`.
`base`/`local_ref` facultatifs fixent HEAD et branche actuels, HEAD détaché compris.
Le jeton lie inspection complète, inventaire de l’index, configuration Git, paramètres du projet, cache approuvé, sélection et empreintes des pointeurs. Un ancien plan périmé est refusé.

## Sélectionner des fichiers et dossiers {#selecting-files-and-folders-036}

Laisser **Sélection LFS** vide pour tous les pointeurs commités ou saisir un chemin relatif par ligne.
Un dossier finit par `/` ; les chemins sont littéraux sans glob. L’aperçu ne montre que les pointeurs correspondants et le nombre `excluded` des autres.
Les objets de cache absents/corrompus hors sélection ne sont pas inspectés et ne bloquent pas les assets choisis. Les fichiers de travail non sélectionnés ne sont pas lus/écrits à cette étape.
Le CLI accepte un tableau JSON UTF-8 par `--paths-file`, fichier d’au plus 32 Kio :

```json
["Content/Maps/", "Content/Textures/logo.uasset"]
```

```sh
cysynck git materialize PROJECT_ID --paths-file selection.json
cysynck git materialize PROJECT_ID --paths-file selection.json --apply true --plan PREVIEW_TOKEN
```

L’API reçoit le même tableau `paths`, omis pour tous les pointeurs. `null` et tableau vide explicites sont refusés. Répéter la sélection lors de l’application.
`selection` vaut null ou un tableau trié/dédupliqué. Les sélecteurs qui se chevauchent incluent un fichier une fois ; chacun doit correspondre à un pointeur commité.
Fichiers ordinaires, chemins inconnus/dangereux et alias de casse sont refusés. Limites avant déduplication : 1–256 chemins, 16 Kio de texte UTF-8 cumulé.
Changer la sélection périme l’aperçu. Le jeton lie la sélection canonique exacte, chevauchements compris, et les paramètres du projet.
Copies de récupération/reçus couvrent seulement les pointeurs choisis en attente ; l’historique expose sélection et exclusions.
Les contrôles reconstituent ce périmètre depuis le commit et les anciens reçus restent lisibles.
La sélection ne contourne ni validation de l’arbre entier, ni contrôles dépôt/index, ni limites globales : un pointeur malformé ou chemin dangereux ailleurs peut bloquer l’inspection. Suspendre les écritures externes.

## États des fichiers et conservation {#file-states-and-preservation}

Seuls les pointeurs LFS v1 canoniques de fichiers commités réguliers, avec entrée index stage zéro identique et attribut commité `filter=lfs`, sont éligibles.
Conversions texte actives et `.git/info/attributes` locaux sont refusés.
Hooks, filtres, fsmonitor et réseau ne sont pas exécutés ; des programmes clean/smudge/process configurés peuvent rester installés sans être lancés.
L’aperçu fournit `files`, `pending`, `materialized`, `skipped`, `bytes`, `errors`, `can_materialize` et les états :

| État | Comportement |
| --- | --- |
| `pointer` | Pointeur exact commité ; exige un objet de cache entièrement vérifié |
| `materialized` | Les octets correspondent déjà à l’asset commité |
| `modified` | Contenu local différent conservé |
| `missing` | Suppression locale conservée, sans recréation |
| `staged` | Changement stagé conservé |
| `index_flags` | Fichier assume-unchanged/skip-worktree conservé |
| `read_only` | Pointeur en lecture seule conservé |
| `attributes` | Filtre LFS absent ou conversion commitée inconnue : fichier ignoré |
| `blocked` | Entrée dangereuse ou erreur empêchant l’application |

Un objet de cache nécessaire absent/corrompu bloque l’opération ; importer les objets vérifiés puis refaire l’aperçu.
Un `lfs.storage` personnalisé nécessite l’approbation explicite du projet.
Fichiers ordinaires, changements stagés indépendants, fichiers ignorés/non suivis, HEAD, références et index sont préservés.
Dépôts ordinaires complets SHA-1/SHA-256 : 10 000 fichiers suivis, 4 096 chemins LFS, 4 Gio par asset, 16 Gio d’assets référencés.
Worktrees liés, dépôts nus/partiels/shallow, index partagés/sparse et opérations Git actives sont refusés.
Chemins, casse, liens, noms de métadonnées et espace de récupération réservé sont contrôlés.

## Publication et fichiers de récupération {#publication-and-recovery-files}

L’application copie d’abord tous les assets nécessaires dans un nouveau dossier privé `PROJECT/.cysynck-lfs/OPERATION_ID/after/`.
Le buffer de 64 Kio et SHA-256 complet bornent la mémoire et vérifient les octets, assets vides/partagés compris.
Prévoir l’espace supplémentaire ; toute la préparation finit avant déplacement d’un pointeur.
Chaque pointeur inchangé est ensuite renommé vers `before/RELATIVE_PATH`, puis son empreinte revérifiée.
L’asset préparé reprend ses permissions et la DACL Windows et n’est publié que si la destination de travail est absente.
Une destination créée en concurrence est conservée. Les pointeurs originaux restent récupérables après succès ; les préparations connues sont déplacées lors de leur publication.
Un reçu privé précède la préparation dans `STATE/git-materializations/PROJECT/OPERATION_ID.json`.
Il enregistre contexte commité, pointeurs sélectionnés, empreintes originales, cache, dossier de récupération et progression.
Le succès renvoie `status:"complete"`, `applied:true`, `files_written`. L’échec conserve reçu incomplet, assets préparés et sources déplacées pour examen.
Aucun retour arrière, nouvelle tentative, retrait ou restauration automatique des fichiers manquants.
Le verrou `.git/cysynck-integration.lock` est partagé avec l’intégration ; il ne verrouille pas les autres clients Git/éditeurs.
La publication multi-fichiers n’est pas une transaction ni une garantie de résistance aux coupures entre fichiers/reçus. Les verrous résiduels sont conservés.
L’espace `.cysynck-lfs/` est non suivi ; l’exclure dans votre workflow si nécessaire. CySynck ne change pas `.gitignore` et ne stage pas ce dossier.

## Contrôler et marquer les reçus comme examinés {#checking-and-acknowledging-receipts}

Les interfaces listent **Reçus de matérialisation LFS** et proposent **Vérifier le reçu LFS**.
Les routes POST restent accessibles par CLI/C/C++, sans changement d’ABI :

| Route | Corps |
| --- | --- |
| `/api/projects/ID/git-materializations` | `{"offset":0,"limit":20}` |
| `/api/projects/ID/git-materialization-check` | `{"receipt":"OPERATION_ID"}` |
| `/api/projects/ID/git-materialization-acknowledge` | `{"receipt":"OPERATION_ID","plan":"CHECK_TOKEN"}` |

```sh
cysynck git materializations PROJECT_ID
cysynck git materialization-check PROJECT_ID OPERATION_ID
cysynck git materialization-acknowledge PROJECT_ID OPERATION_ID --plan CHECK_TOKEN
```

Le contrôle compare le reçu aux pointeurs commités immuables, HEAD, entrées/flags stagés, SHA-256 des assets de travail, empreintes originales et préparations connues.
Vérifier les fichiers existants n’exige pas le cache ; la reprise l’examine si un asset préparé manque.
Entrées inconnues, liens, originaux altérés, préparation incomplète et verrou d’intégration bloquent la revue et sont conservés.
`index_metadata_matches` distingue la comparaison binaire de l’ancien index : un rafraîchissement du cache de stat Git peut changer ses octets sans changer entrées/flags et ne bloque pas seul la revue.
Le jeton fixe aussi l’index binaire observé actuellement.
`position` vaut `before`, `after`, `partial`, `interrupted`, `changed`, `unknown` ; chaque fichier montre les empreintes travail/original/préparation.
`before` signifie que pointeurs et permissions sont revenus, avec des dates éventuellement nouvelles si recopiés ; `after` exige aussi les originaux inchangés dans la récupération.
`context_matches` doit être vrai.
Seul un reçu incomplet non examiné dans un état complet exact avant/après fournit `can_acknowledge:true` et un jeton frais.
La revue écrit seulement les métadonnées du reçu ; elle conserve statut/erreur, sans reprise, retour arrière, restauration, retrait de verrou ou suppression de copies.
Un changement périme le jeton et une revue terminée n’est pas rejouable. Les états partiels/interrompus nécessitent une récupération ; une publication vérifiée peut utiliser la reprise ci-dessous.
Le catalogue retourne 20 entrées par défaut, 100 au plus, sur 10 000 reçus scannés.
Les voisins malformés sont signalés séparément et noms non indexés conservés. Lecture des reçus/métadonnées de revue : 16 Mio.
[La restauration des pointeurs](doc:git-dehydration) est l’inverse explicite et conserve les assets développés.
Ses reçus de schéma 2 partagent catalogue/contrôle avec `operation:"dehydrate"` et les sens avant/après propres à cette opération.

## Reprendre une publication vérifiée {#resuming-verified-publication-037}

Le contrôle fournit `can_resume:true` quand une opération incomplète non examinée peut finir avec assets vérifiés et pointeurs originaux exacts.
Cela couvre aussi les pointeurs de travail inchangés et préparations absentes récupérables du cache.
Choisir **Prévisualiser la reprise LFS**, examiner les compteurs puis **Reprendre la publication vérifiée** dans les interfaces bilingues.

```sh
cysynck git materialization-resume PROJECT_ID OPERATION_ID
cysynck git materialization-resume PROJECT_ID OPERATION_ID --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/ID/git-materialization-resume` reçoit `{"receipt":"OPERATION_ID"}`, puis `apply:true`/`plan`.
L’aperçu renvoie `already_published`, `pending_publications`, `check`, `can_resume`, `resumed:false` et un jeton liant reçu, profil/projet, configuration,
index binaire, entrées/flags, HEAD et toutes les empreintes contrôlées.
L’application prend le verrou, refait l’inspection, rehashe chaque préparation/pointeur restant, conserve permissions/DACL et publie uniquement vers une destination absente.
Les destinations existantes et assets déjà publiés ne sont pas réécrits. Les assets entièrement préparés n’exigent pas le cache.
Aucun helper LFS, filtre ou téléchargement. La sélection originale est conservée ; aucun élargissement par une nouvelle liste.
Si tout était publié avant l’interruption, l’application finit seulement le journal.
Le succès donne `resumed:true`, `status:"complete"`, le `files_written` exact ; `resume` facultatif note tentative, dates et précédent compteur.
L’erreur originale reste dans l’historique et le catalogue expose la reprise. Les opérations complètes/examinées ne sont pas reprenables ; changements de fichiers/configuration périment l’aperçu.
Un échec conserve reçu incomplet, originaux, préparations restantes et fichiers publiés. La dernière erreur est conservée sur 2 048 octets.
Un nouvel aperçu peut poursuivre ; après 1 000 tentatives, examen manuel exigé.
Aucun retour arrière, nouvelle tentative automatique ou retrait de verrou. Assets préparés corrompus, originaux modifiés, entrées inconnues et contexte Git changé nécessitent une récupération manuelle.

## Reprendre la préparation et les états mixtes {#resuming-staging-and-mixed-states-038}

La même route traite un mélange d’assets publiés, destinations absentes avec pointeurs préservés et pointeurs encore au travail.
Le pointeur de travail doit correspondre à l’empreinte originale, date/permissions comprises, et ne pas déjà avoir de copie préservée.
Un pointeur recopié avec une date nouvelle peut être examiné dans l’état `before`, mais la reprise ne peut pas le déplacer automatiquement.
Les doublons travail/préservation restent intacts pour examen.
Si `after/` manque, l’aperçu vérifie SHA-256 et taille dans le cache actuellement approuvé, et lie chemin/empreinte complète du cache au jeton.
Les préparations présentes sont réutilisées ; les assets publiés n’ont pas besoin du cache.
Un objet nécessaire absent/corrompu bloque avec `resume_errors` par chemin sans empêcher la revue d’un état avant/après exact par ailleurs.
L’aperçu ajoute `pending_staging`, `staging_bytes`, `pointers_to_preserve`.
Chaque fichier expose `resume_action` (`keep`, `publish`, `stage_and_publish`, `blocked`) et la source `cache` examinée si nécessaire ; CyUI/Web affichent ces raisons en anglais/français.
L’application complète et flush toutes les préparations manquantes avant de déplacer un pointeur.
Les dossiers absents sont créés privés, les chemins réservés exclusivement, les octets copiés/rehashés puis contexte et fichiers revérifiés.
Les pointeurs exacts sont déplacés vers des `before/` absents, sans remplacement, puis les assets publiés vers des destinations absentes.
Sélection, erreurs, HEAD, index et configuration restent conservés.
Un changement de cache périme le jeton ; une préparation présente incomplète/corrompue reste pour examen au lieu d’être écrasée.
Un échec de préparation garde la phase `staging`, un échec de publication `publishing`.
La reprise fonctionne même si l’opération initiale s’est arrêtée avant de créer sa récupération.
Prévoir de l’espace pour les assets à repréparer ; aucun téléchargement ni rechargement automatique du cache.
Suspendre les écritures externes : ni contrôle ni publication ne forme une transaction. Les validations natives Linux/macOS et en cas de coupure restent nécessaires.
