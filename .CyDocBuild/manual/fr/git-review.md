---
id: git-review
title: Examiner les branches Git importées
description: Comparer les arbres commités local et distant sans modifier les fichiers de travail.
section: Git
order: 32
parent: git-setup
sourceRevision: 1
---

# Examiner les branches Git importées {#review-imported-git-branches}

CySynck compare le `HEAD` commité local à une référence déjà importée `refs/remotes/cysynck/PEER/BRANCH`.
La revue est locale et en lecture seule : aucun checkout, fusion, rafraîchissement d’index ou téléchargement d’objet Git/LFS absent.
Les changements locaux et stagés restent locaux. Une branche valide sans premier commit est acceptée : `base:null`, `relation:"unborn"`, zéro commit propre au côté local et arbre complet du pair comparé au vide.
Voir [l’initialisation](doc:git-setup).

```sh
cysynck git branches PROJECT_ID
cysynck git review PROJECT_ID PEER_ID BRANCH
cysynck git review-file PROJECT_ID PEER_ID BRANCH 'relative/path.cpp'
```

Le bilan renvoie les commits local/importé, l’ancêtre commun éventuel, les nombres de commits propres à chaque côté et la relation
`identical`, `fast_forward`, `local_ahead`, `diverged` ou `unrelated`.
La liste compare directement deux arbres commités ; ce n’est ni une simulation de fusion à trois versions ni un diff limité à l’ancêtre commun.
Les renommages apparaissent comme suppression/ajout. Changements de mode, liens symboliques Git et sous-modules restent visibles.
La [revue à trois versions](doc:git-merge-review) compare séparément les branches à leur ancêtre et propose des textes bornés avec marqueurs diff3.
CyUI propose les vues côte à côte/unifiées ; le Web montre la liste et le patch unifié. Les deux interfaces sont anglais/français.

## Aperçus de texte et de métadonnées {#text-and-metadata-previews}

Le texte est limité à 512 Kio par côté, UTF-8 valide sans NUL. Gros fichiers, binaires, texte non UTF-8 et sous-modules renvoient des métadonnées et une explication.
Les cibles des liens Git viennent du blob commité, sans suivre de lien du système de fichiers.
Les pointeurs LFS canoniques exposent SHA-256 et taille de l’asset ; le patch compare les pointeurs, pas les contenus d’assets.
Git fournit les tailles avant lecture. Les listes sont délimitées par NUL et les chemins sont des pathspecs littéraux, conservant espaces, Unicode et jokers.
Les patches citent les chemins selon Git. Diff externe, textconv, objets de remplacement et protocoles réseau sont désactivés.
Référence : [format brut de Git diff](https://git-scm.com/docs/git-diff).
Au plus 10 000 fichiers modifiés ou références importées, chemins de 4 096 octets UTF-8 et patches de 4 Mio ; un dépassement échoue explicitement sans liste partielle.

## Contexte de revue stable {#stable-review-context}

Les aperçus peuvent fournir `base`, `head`, `local_ref` du bilan. Un changement de référence fixée est refusé et exige un rafraîchissement.
Les références sont vérifiées de nouveau après comparaison ; les interfaces les fixent et ignorent les réponses périmées.

```sh
cysynck git review-file PROJECT_ID PEER_ID BRANCH path.cpp --base LOCAL_COMMIT --head PEER_COMMIT
cysynck git review-file PROJECT_ID PEER_ID BRANCH path.cpp --base unborn --head PEER_COMMIT --local-ref refs/heads/main
```

`local_ref` est la cible symbolique de HEAD, ou null si détaché (`--local-ref detached`). Le fixer détecte aussi un changement entre branches sans premier commit.
Une cible HEAD corrompue ou non commit échoue et n’est pas assimilée à un historique vide.
L’API locale protégée et le SDK C/C++ proposent les POST suivants :

| Route | Corps |
| --- | --- |
| `/api/projects/ID/git-branches` | `{}` |
| `/api/projects/ID/git-review` | `{"peer":"PEER","branch":"main"}` |
| `/api/projects/ID/git-review-file` | `{"peer":"PEER","branch":"main","path":"file.cpp","base":"...","head":"..."}` |

La vue propose séparément [l’avance rapide après revue](doc:git-integration) et [la fusion divergente](doc:git-divergent-merge) pour les dépôts éligibles.
[L’historique paginé et la revue par commit](doc:git-history) couvrent les commits racine et le choix d’un parent de fusion avec les mêmes limites d’aperçu.
