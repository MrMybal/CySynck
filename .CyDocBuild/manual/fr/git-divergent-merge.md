---
id: git-divergent-merge
title: Fusion Git divergente après revue
description: Examiner le résultat complet, choisir les conflits texte et publier explicitement un commit à deux parents.
section: Git
order: 37
parent: git-integration
sourceRevision: 1
---

# Fusion Git divergente après revue {#reviewed-divergent-git-merge-027}

Dans la comparaison de branche, choisir **Prévisualiser la fusion du dépôt**, puis **Appliquer la fusion examinée** quand l’aperçu complet est prêt.
CyUI/Web sont anglais/français ; CLI et SDK partagent l’opération :

```sh
cysynck git merge PROJECT_ID PEER_ID BRANCH
cysynck git merge PROJECT_ID PEER_ID BRANCH --apply true --plan PREVIEW_TOKEN
```

POST `/api/projects/PROJECT_ID/git-merge` reçoit `{"peer":"PEER","branch":"main"}`, puis `apply:true`/`plan` pour appliquer.
`base`, `head`, `local_ref` facultatifs fixent le contexte comme pour [l’avance rapide](doc:git-integration).
L’action est explicite : les échanges de bundles en arrière-plan n’appliquent jamais de fusion.

## Éléments examinés {#what-is-reviewed}

Les historiques SHA-1/SHA-256 doivent être réellement divergents, complets et avoir exactement un ancêtre commun.
Avance rapide et branche sans premier commit utilisent `git integrate`.
Historiques indépendants, bases multiples, HEAD détaché et dépôts incomplets nécessitent un traitement manuel.
Le résultat complet est calculé par [git merge-tree --write-tree](https://git-scm.com/docs/git-merge-tree)
dans un dépôt nu privé `STATE/git-merge-work/RANDOM_ID/`.
Un fichier alternates en lecture seule donne accès aux objets source ; arbres/blobs nouveaux sont écrits uniquement dans cet espace privé.
L’aperçu conserve HEAD, références, index, fichiers, configuration et objets source et ne crée aucun commit de fusion.
Il ajoute aux champs d’intégration `integration_kind:"merge"`, `merge_base`, `merge_clean`, `conflicts`, `result_tree`, `result_commit:null`, `merge_identity`, `merge_message`.
`files_after` contient l’inventaire complet si propre. Les chemins de conflit viennent des enregistrements Git délimités par NUL.
Un résultat non résolu/non pris en charge n’a pas de jeton applicable. Utiliser aussi [la revue à trois versions](doc:git-merge-review).

## Choisir des versions commitées pour les conflits {#choosing-committed-files-for-conflicts-025}

Pour les conflits texte UTF-8 acceptés, choisir **Utiliser le fichier local** ou **Utiliser le fichier du pair** ; un côté absent est une suppression locale/du pair.
Examiner d’abord les trois versions. Le choix remplace le **fichier entier**, même ses modifications sans conflit.
Les autres fichiers gardent leur résultat automatique. L’éditeur peut combiner les changements dans un fichier.
Aucun choix par défaut. Modifier/retirer un choix exige un nouvel aperçu réussi.
Chaque conflit doit avoir un choix ; une sélection partielle reste bloquée.
Les choix ne peuvent remplacer des fichiers propres ni désigner des objets/chemins arbitraires.
Le CLI accepte `--resolutions FILE.json`, fichier JSON UTF-8 :

```json
[
  {"path":"Source/Game.cpp","side":"local"},
  {"path":"Notes.txt","side":"peer"}
]
```

```sh
cysynck git merge PROJECT_ID PEER_ID BRANCH --resolutions choices.json
cysynck git merge PROJECT_ID PEER_ID BRANCH --resolutions choices.json --apply true --plan PREVIEW_TOKEN
```

API/SDK reçoivent le même tableau `resolutions`, à répéter exactement à l’application.
Le jeton lie ordre canonique des chemins et arbre résultant entier.
Doublons, alias de casse, chemins inconnus, champs supplémentaires et côtés inconnus sont refusés.
`local`/`peer` ne contiennent que `path`/`side` ; `edited` exige aussi une chaîne `content`.
Limites : 10 000 chemins et 1 Mio sérialisé. L’avance rapide refuse ce champ.
L’aperçu ajoute `conflict_files` avec métadonnées local/pair nullables, `resolutions` canoniques, `unresolved_conflicts`, `result_ready`.
`merge_clean` décrit la fusion automatique originale et reste faux après résolution explicite.
`result_ready:true` indique un arbre complet ; `can_integrate` exige aussi tous les contrôles du dépôt.
`conflicts` conserve les chemins originaux. Un résultat non résolu n’expose aucun inventaire/jeton applicable ; son arbre diagnostique peut contenir des marqueurs.
Le dépôt privé charge le résultat dans son index, applique identités/modes ou suppressions avec [update-index --index-info](https://git-scm.com/docs/git-update-index#_using_index_info), puis écrit/valide l’arbre entier.
L’index, les fichiers et les objets source restent inchangés à l’aperçu. Application/contrôle des reçus recalculent les choix.
Conflits ajout/ajout et modification/suppression texte sont acceptés.
Chemins structurels/synthétiques, binaires/non UTF-8, texte trop gros et autres restrictions ci-dessous exigent Git manuel.

## Modifier le texte d’un conflit {#editing-conflict-text-026}

**Modifier le texte fusionné** ouvre les trois versions en lecture seule et un brouillon initialisé avec la proposition.
En modification/suppression, la version survivante initialise le brouillon.
Les brouillons sont gardés en mémoire tant que la comparaison reste ouverte ; rafraîchir/changer de branche ou projet les abandonne.
Retirer les marqueurs, combiner les passages puis **Utiliser le texte modifié** et refaire un aperçu.
Annuler ferme sans changer le choix enregistré. Aperçu/application sont désactivés pendant l’éditeur ; sauvegarder le brouillon périme l’ancien plan.
Les autres fichiers gardent leur résultat/choix. L’éditeur ne devine pas les lignes gagnantes.
Le CLI et la route JSON C/C++ acceptent :

```json
[
  {"path":"Source/Game.cpp","side":"edited","content":"combined text\n"},
  {"path":"Notes.txt","side":"peer"}
]
```

Le texte doit être UTF-8 sans NUL, au plus 512 Kio par fichier et dans le tableau sérialisé de 1 Mio.
Une chaîne vide crée un fichier vide, pas une suppression. Les pointeurs LFS canoniques sont refusés.
Les lignes commençant après espaces/tabulations/CR éventuels par `<<<<<<<`, `|||||||`, `=======`, `>>>>>>>`, même prolongés, sont refusées par précaution.
Un texte légitime avec ces lignes nécessite un choix de fichier commité ou Git manuel. Ce contrôle aide à repérer les marqueurs restants sans remplacer la revue.
Si les deux côtés existent, leurs modes doivent être identiques pour l’édition ; sinon choisir un côté ou résoudre les permissions dans Git.
Avec un côté absent, le mode survivant est repris. `conflict_files[].editable` expose cette condition.
[hash-object -w --no-filters --stdin](https://git-scm.com/docs/git-hash-object) crée les blobs édités uniquement dans le dépôt privé.
Le résultat complet est validé avant jeton. Requêtes/application/reçus utilisent les octets exacts, CRLF et absence de saut final compris.
La zone Web utilise LF ; les hashes portent sur le texte soumis. L’application répète ce contenu/jeton, importe les objets examinés et effectue le checkout vérifié.
Le texte édité reste privé dans le reçu pour recalcul déterministe avant/après interruption.
Les résumés n’exposent que `edited_files` et `resolved_conflicts`. Ces reçus exigent 0.26+ ; les anciens reçus/choix commités restent compatibles.

Détection de renommage et renormalisation sont désactivées.
Les chemins sont validés dans arbres local, pair, ancêtre et résultat, même inchangés.
Renommages de casse, alias, liens, sous-modules, pointeurs LFS et conversions actives exigent une intervention.
Règles de fusion personnalisées/par défaut et `.git/info/attributes` non vide sont refusés.
Pilotes, filtres, hooks, rerere et réseau sont désactivés dans l’espace privé ; le fichier d’attributs utilisateur effectif est neutralisé, les attributs commités/système restent contrôlés.
La casse est ordinale sous Windows, ASCII replié sous POSIX ; normalisation Unicode et alias spécifiques nécessitent validation native.
Limites régulières : 1 Gio par blob, 10 000 fichiers et 16 Gio par arbre.
Un même chemin modifié différemment exige UTF-8 sans NUL, 512 Kio par côté et 16 Mio de textes se chevauchant au total.
Un binaire modifié d’un seul côté peut passer par identité d’objet.
Git est limité à 120 secondes par commande et doit fournir `merge-tree --write-tree --merge-base` et `check-attr --source` ; version locale de référence 2.45.1.

## Choisir des régions de conflit {#choosing-individual-conflict-regions-027}

**Choisir les régions de conflit** propose les blocs diff3 non ambigus : ancêtre, local et pair.
Choisir **Région locale**, **Région du pair** ou **Les deux (local puis pair)** pour chaque région, puis **Construire le brouillon des régions choisies**.
Cela remplace le texte de l’éditeur en conservant les changements automatiques autour, sans sauver ni appliquer.
Examiner/retoucher, utiliser le texte édité puis refaire l’aperçu du dépôt.
Un brouillon sauvegardé peut être rouvert ; les sélecteurs de régions repartent non résolus à chaque ouverture. Annuler garde le choix précédent.
Ces choix portent sur le texte du fichier, pas sur la structure du dépôt ou les permissions.
POST `git-merge-file` ajoute `regions` : `available`, `reason`, `count`, `token`, `segments` ordonnés.
Les contextes ont `kind:"context",text` ; les conflits `kind:"conflict"`, `id` à partir de zéro, `local`, `ancestor`, `peer`.
Le nombre est exact même si le code de sortie Git plafonne à 127 ; au plus 256 régions par fichier.
Des lignes source commençant par sept caractères de marqueur désactivent l’assistant pour ne pas confondre contenu et délimiteurs.
Propositions inconnues, ambiguës, malformées ou trop grandes n’offrent ni segments sélectionnables ni jeton ; édition manuelle et fichiers commités restent possibles selon leurs limites.
Renvoyer contexte, chemin, `region_token` et `region_choices` ordonnés (`local`, `peer`, `both`) sur la même route.
Le cœur régénère/vérifie le jeton puis donne `composed_text` ; chaque région exige un choix valide, complet et actuel.
Les segments du client ne sont jamais utilisés. Le tableau est limité à 256 entrées et 16 Kio ; le résultat reste limité à 512 Kio, en lecture seule.

```powershell
cysynck git merge-file team alice main Source/Game.cpp
# Enregistrer ["local", "both"] dans regions.json et utiliser regions.token de la réponse.
cysynck git merge-file team alice main Source/Game.cpp --regions regions.json --region-token TOKEN
```

`both` concatène local puis pair sans déduplication ni séparateur supplémentaire.
Régions et contexte gardent les octets de [merge-file --diff3 --stdout](https://git-scm.com/docs/git-merge-file), CRLF compris.
Git peut ajouter un saut près d’un marqueur si une entrée n’avait pas de saut final ; la composition le conserve, à retoucher au besoin.
Le Web normalise toujours en LF. Le jeton identifie une proposition, pas un droit d’application.
Seuls le texte final `edited` et le plan d’intégration déterminent application/reçus ; les choix de régions UI ne sont pas stockés.

## Appliquer le résultat examiné {#applying-a-reviewed-result}

`user.name`/`user.email` Git valides sont exigés et affichés. Retirer les surcharges d’environnement auteur/committer.
Le jeton lie identité, message, arbre entier, choix, branche, configuration, index et métadonnées locales indépendantes.
Dates et noms temporaires aléatoires sont exclus pour garder un aperçu stable à état constant.
L’application garde les contrôles index propre/octet brut, collisions locales et verrou de l’avance rapide.
Elle recalcule après verrou et avant publication des objets.
Un commit non signé à deux parents est créé en privé avec [commit-tree](https://git-scm.com/docs/git-commit-tree) : local d’abord, pair ensuite.
Un bundle incrémental local de 64 Mio au plus importe les objets immuables sans changer les références ni contacter le réseau.
L’identifiant du nouveau commit est noté dans le reçu incomplet avant l’avance rapide Git locale vers lui.
Git met à jour HEAD, index, reflogs, `ORIG_HEAD` et fichiers.
Les contrôles finaux vérifient octets exacts, index propre, permissions exécutables POSIX et métadonnées locales conservées.
Succès : `result_commit`, `applied:true`, `status:"complete"` ; références de suivi du pair conservées.
Opération Git en cours, choix manquants, staging, fichiers suivis modifiés et collisions locales sont refusés.
Les fichiers ordinaires non suivis/ignorés indépendants restent intacts.
Fermer les clients Git et éviter les écritures : le verrou CySynck ne rend pas les autres logiciels transactionnels.
Voir [la fraîcheur locale](doc:git-integration#local-data-freshness).

## Reçus et interruption {#receipts-and-interruption}

Les reçus partagent `git-integrations`, `git-integration-check`, `git-integration-acknowledge` et `STATE/git-integrations/PROJECT/ID.json`.
Ils ajoutent type de fusion, arbre, ancêtre, identité et commit résultant.
Utiliser 0.24+ pour ces reçus ; les anciennes avances rapides restent compatibles.
Les choix explicites exigent 0.25+, gardent `resolutions` canoniques et un résumé `resolved_conflicts`.
Les contrôles recalculent le résultat en espace privé et vérifient reçu, parents ordonnés, arbre, identités/message du commit disponible,
branche, index, octets suivis et métadonnées locales.
Une interruption peut être examinée dans l’état exact avant/après. La revue conserve statut/erreur sans nouvelle tentative, reset, commit ou retour arrière.
Import d’objets, checkout et reçus ne forment pas une transaction : un échec peut laisser objets sans référence, checkout terminé ou autre état partiel.
Le reçu et le dépôt sont conservés pour examen. Les verrous Git et `.git/cysynck-integration.lock` ne sont jamais retirés par revue.
Le nettoyage normal retire individuellement les fichiers possédés puis dossiers vides.
Liens ou entrées inattendues l’arrêtent ; un crash peut laisser objets/bundles privés.
Aucune suppression récursive ne suit l’alternates vers le dépôt source. Inspecter les espaces abandonnés avant nettoyage manuel.
