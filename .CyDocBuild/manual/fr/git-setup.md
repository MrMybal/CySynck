---
id: git-setup
title: Initialiser Git et examiner le premier import
description: Créer un dépôt local après aperçu, puis inspecter la branche d’un pair.
section: Git
order: 30
sourceRevision: 1
---

# Initialiser Git et examiner le premier import {#git-setup-and-the-first-import}

CySynck peut initialiser explicitement un dossier de projet existant puis inspecter une branche importée avant tout premier commit local.
Le CLI, l’API locale, le SDK C/C++, CyUI et le Web bilingue fournissent les mêmes opérations. Git doit être installé.

## Initialiser le dossier du projet {#initialize-a-project-folder}

Créer un projet `git-bundles` avec le même identifiant de projet et le dossier d’échange partagé du pair. Le dossier du projet doit exister.
Choisir la branche initiale (`main` par défaut) et le format d’objets (`sha1` par défaut, ou `sha256` pour correspondre au dépôt du pair).

```sh
cysynck project add Team /absolute/team --id team --mode git-bundles --exchange-path /absolute/exchange --interval 0
cysynck git setup team --branch main --object-format sha1
# Copier le plan renvoyé en gardant les mêmes options :
cysynck git setup team --branch main --object-format sha1 --apply true --plan PREVIEW_TOKEN
```

L’aperçu ne crée aucune métadonnée Git. Le plan lie identifiant, racine canonique, branche et format.
L’application réserve exclusivement un nouveau `.git`, lance Git avec modèles vides et hooks/protocoles réseau désactivés, puis vérifie identité et HEAD sans commit.
Aucun commit, staging ni checkout du pair n’est créé ; les fichiers existants sont conservés.
Désactiver les variables `GIT_*` propres au dépôt pour empêcher toute redirection héritée du dépôt, de l’index ou du stockage d’objets.
Toute entrée `.git` existante, fichier de worktree, lien, métadonnée invalide ou dépôt nu est conservé et interdit une réinitialisation.
Une métadonnée apparue après aperçu bloque l’application. Un échec après création conserve les métadonnées pour examen et le signale ; aucune réparation/suppression automatique.
Dans Échange Git ou le panneau Web, choisir branche/format, préparer puis **Initialiser le dépôt**. Modifier les options exige un nouvel aperçu.
Les dépôts existants accèdent directement aux échanges.

## Examiner le premier import d’un pair {#inspect-the-first-peer-import}

```sh
cysynck git inbox team
cysynck git import team PEER BUNDLE
cysynck git branches team
cysynck git review team PEER main
cysynck git history team PEER main --limit 50
cysynck git review-file team PEER main relative/file.cpp
```

L’import vérifie bundle et inventaire LFS puis publie une référence de suivi distante, sans remplir les fichiers de travail.
Sans commit local, la revue indique `relation:"unborn"`, `base:null` et compare l’arbre courant entier du pair à un arbre vide, y compris les fichiers inchangés dans son dernier commit.
L’historique et les commits racine/de fusion gardent leurs comparaisons par parent. Fichiers modifiés et index local restent intacts.
[L’extraction de révision](doc:git-extraction) copie séparément un arbre importé et ses assets vérifiés dans un nouveau dossier, après aperçu.
`local_ref` contient la cible symbolique de HEAD. Le transmettre avec `base`/`head` pour les continuations et aperçus stables.
`--base unborn` correspond au JSON null ; `--local-ref refs/heads/main` fixe la branche locale.
Changer de branche, créer le premier commit ou avancer la référence du pair périme les requêtes. Une cible HEAD corrompue échoue explicitement.
`git status team` fournit `unborn`, `head` nullable, `branch`, `local_ref`.
Après initialisation/revue, un projet propre peut [intégrer explicitement le premier import](doc:git-integration).
La création des commits ordinaires relève de Git ; les branches divergentes éligibles disposent aussi d’une [fusion après revue](doc:git-divergent-merge).
L’import et la revue seuls préservent le travail existant.

## API et SDK {#api-and-sdk}

POST `/api/projects/ID/git-setup` reçoit `{"branch":"main","object_format":"sha1"}` ; ajouter `apply:true` et `plan` pour appliquer.
La réponse comprend `root`, `branch`, `object_format`, `metadata_present`, `can_initialize`, `applied`, `plan` et `reason` anglais.
La simple présence de `.git` désactive l’initialisation sans prouver la validité du dépôt.
La même API protégée et l’ABI JSON UTF-8 n’exigent aucune nouvelle version d’ABI.
Consulter [la revue de branche](doc:git-review) et [l’historique](doc:git-history) pour les limites/continuations.
Références : [références symboliques](https://git-scm.com/docs/git-symbolic-ref), [validation des révisions](https://git-scm.com/docs/git-rev-parse).
