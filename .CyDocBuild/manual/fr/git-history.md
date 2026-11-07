---
id: git-history
title: Historique importé et revue des commits
description: Parcourir une branche importée et comparer un commit au parent choisi.
section: Git
order: 33
parent: git-review
sourceRevision: 1
---

# Historique importé et revue des commits {#imported-git-history-and-commit-review}

CySynck liste les commits accessibles depuis une branche importée et compare un commit à l’un de ses parents.
Cette vue complète la comparaison HEAD local/tête du pair. Le cœur C++ sert CLI, SDK, CyUI et Web bilingue.

```sh
cysynck git history PROJECT PEER BRANCH --limit 50
cysynck git history PROJECT PEER BRANCH --limit 50 --offset 50 --base LOCAL_HEAD --head PEER_HEAD
cysynck git commit PROJECT PEER BRANCH FULL_COMMIT_ID
cysynck git commit-file PROJECT PEER BRANCH FULL_COMMIT_ID relative/path.cpp --parent 1
```

## Pages d’historique {#history-pages}

L’ordre est topologique, avec les commits des parents secondaires de fusion.
Chaque entrée comporte `oid`, `parents` ordonnés, `subject`, `author`, `committer` ; les identités contiennent nom, adresse et date ISO enregistrés.
Mailmap, notes et programmes de vérification des signatures sont désactivés. Ces métadonnées ne prouvent pas la confiance.
Pages de 50 commits par défaut, `limit` 1–100, `offset` 0–100 000.
La réponse fournit `commits`, `offset`, `limit`, `next_offset`, `truncated` et le contexte `base`, `head`, `local_ref`, `peer`, `branch`, `tracking_ref`.
Les pages suivantes exigent les `base`/`head` initiaux. Un changement de HEAD ou référence importée exige de rafraîchir la revue.
`next_offset:null` indique la fin ou la borne d’offset ; `truncated:true` distingue la borne de la véritable fin.

L’historique fonctionne avant le premier commit local : `base:null` ou `--base unborn`, avec `head` et `local_ref` initiaux.
Une autre branche sans commit ou un premier commit périme ce contexte. HEAD détaché utilise `local_ref:null` ou `--local-ref detached`.
Limites : 16 Kio par champ de métadonnées, 256 parents et 8 Mio de sortie capturée.
UTF-8 invalide et métadonnées incomplètes échouent. Un dépôt complet, non shallow, est exigé pour ne pas confondre une frontière tronquée avec un commit racine.

## Comparaisons par commit {#per-commit-comparisons}

La sélection exige un identifiant SHA-1/SHA-256 complet accessible depuis la branche importée choisie.
Abréviations, expressions de révision et objets locaux sans rapport sont refusés.
Le premier parent est utilisé par défaut. `parent:2` compare au second parent d’une fusion, etc. Les indices commencent à un.
Un commit racine utilise `parent:0` et compare récursivement au vide sans écrire un objet arbre vide.
Les commits racine, vides et de fusion restent consultables. Il s’agit de deux arbres, pas d’un diff combiné ni d’une simulation de fusion.
La réponse ajoute `commit`, `parent`, `comparison_base` (null pour la racine), `comparison_head` ; `base`/`head` gardent le contexte de branche.
Listes et aperçus reprennent les [limites de revue](doc:git-review) : chemins littéraux, 512 Kio de texte par côté, métadonnées binaires/sous-modules et reconnaissance des pointeurs LFS.
Diff externe, textconv, hooks, objets de remplacement et réseau sont désactivés.
Aucun checkout, fusion, changement de référence ou rafraîchissement de l’index ; le travail non commité reste intact.
Références : [formats Git log](https://git-scm.com/docs/git-log), [comparaisons d’arbres](https://git-scm.com/docs/git-diff).
L’arbre vide est hashé selon le format du dépôt sans écrire d’objet.

## API et SDK {#api-and-sdk}

Les SDK C/C++ exposent les POST authentifiés :

| Route | Corps |
| --- | --- |
| `/api/projects/ID/git-history` | `{"peer":"PEER","branch":"main","limit":50,"offset":0}` |
| `/api/projects/ID/git-commit` | `{"peer":"PEER","branch":"main","commit":"FULL_OID","parent":1}` |
| `/api/projects/ID/git-commit-file` | `{"peer":"PEER","branch":"main","commit":"FULL_OID","path":"file.cpp","parent":1}` |

Toutes acceptent `base`/`head` facultatifs, obligatoires pour la continuation d’historique.
Les interfaces les fournissent automatiquement et rejettent les réponses périmées. Pour une racine, omettre `parent` ou mettre zéro.
Appliquer un commit ou intégrer une branche reste une action explicite distincte.
