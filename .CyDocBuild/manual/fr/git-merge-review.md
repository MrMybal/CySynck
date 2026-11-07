---
id: git-merge-review
title: Revue Git commitée à trois versions
description: Examiner ancêtre, version locale et version du pair avant toute intégration.
section: Git
order: 34
parent: git-review
sourceRevision: 1
---

# Revue Git commitée à trois versions {#three-way-committed-git-review-023}

Examiner les changements des deux branches depuis leur ancêtre commun puis demander une proposition de texte bornée par fichier.
CLI, CyUI, Web et SDK C/C++ partagent ces routes en lecture seule. Les interfaces sont anglais/français.

```sh
cysynck git merge-review PROJECT_ID PEER_ID BRANCH
cysynck git merge-file PROJECT_ID PEER_ID BRANCH 'relative/path.cpp'
```

Importer d’abord la branche du pair. Dans sa comparaison, choisir **Examiner les changements à trois versions**, puis **Aperçu du fichier à trois versions**.
Proposition, ancêtre, version locale commitée et version du pair ont des onglets séparés. Les modifications stagées/non commitées sont exclues et conservées.

## Périmètre et résultats {#scope-and-results}

L’aperçu résout toutes les bases de fusion et exige exactement un ancêtre commun.
Branche locale sans commit, historiques sans lien et plusieurs bases renvoient `can_review:false` avec `unborn`, `unrelated`, `multiple_bases`.
Les dépôts shallow et partiels/promisor sont refusés. SHA-1 et SHA-256 sont acceptés.
La liste unit les changements ancêtre→local et ancêtre→pair. Chaque entrée fournit `ancestor_info`, `local_info`, `peer_info` : existence, mode, objet et taille.
`classification` vaut `local_only`, `peer_only`, `identical`, `both_changed`, `structural` ; `both_changed` seul ne signifie pas conflit texte.
Collisions fichier/dossier et alias de casse des chemins listés sont signalés pour revue structurelle.
La comparaison utilise le repli ASCII sous POSIX et l’ordre ordinal Windows sous Windows ; les chemins inchangés ailleurs ne sont pas vérifiés ici.
Les renommages restent suppression/ajout.

Le fichier choisi renvoie `texts:{ancestor,local,peer}` quand possible, ainsi que `candidate`, `candidate_mode`, `result_exists`, `clean`, `conflicts`, `kind`, `reason`.

| `kind` | Sens |
| --- | --- |
| `clean` | Proposition sans marqueur de conflit produit par cette comparaison |
| `conflict` | Marqueurs diff3 `LOCAL`, `ANCESTOR`, `PEER` |
| `delete` | Les versions impliquent une suppression ; `result_exists:false` |
| `manual` | Revue manuelle nécessaire, sans candidat |

`conflicts` vaut zéro pour une proposition propre/suppression, null sans proposition, ou le nombre Git plafonné à 127.
Un côté absent vaut chaîne vide dans `texts` : les métadonnées distinguent absence et fichier vide.
`candidate_mode` décrit le mode proposé sans appliquer de permissions.
Une proposition propre ne concerne qu’un chemin : elle ne valide ni fusion du dépôt, renommages, bases multiples, tests ou assets LFS.
Une [fusion divergente après revue](doc:git-divergent-merge) ou une [avance rapide](doc:git-integration) sont des opérations séparées.

## Limites et attributs Git {#bounds-and-git-attributes}

Texte UTF-8 sans NUL, 512 Kio par côté. Binaires/non UTF-8, gros blobs, pointeurs LFS canoniques, liens, sous-modules et types inconnus renvoient une raison de revue manuelle.
Conflits modification/suppression et permissions ambiguës nécessitent aussi une intervention. Aucun asset LFS n’est téléchargé ni développé.
Les attributs commités des trois révisions et règles locales/globales/système sont examinés.
Pilotes de fusion personnalisés/par défaut, règles binaires et conversions actives `filter`, `text`, `eol`, `working-tree-encoding`, `ident`, `crlf` imposent une revue manuelle.
`merge` simple et attributs de checkout explicitement désactivés sont acceptés. Aucun pilote personnalisé, diff externe, textconv, hook ou fsmonitor n’est exécuté.
Pour les fichiers non triviaux acceptés, [Git merge-file](https://git-scm.com/docs/git-merge-file) utilise `--stdout`, `--diff3`, Myers et des libellés fixes sur trois copies temporaires privées.
Git doit proposer `merge-file --diff-algorithm` et `check-attr --source` ; la version de référence locale est Git 2.45.1.
Limites : proposition sous 4 Mio, liste sous 8 Mio, union de 10 000 chemins de 4 096 octets, délai Git de 120 secondes.
Fichiers, index, références, configuration et base d’objets source restent inchangés ; ni objet de fusion ni reçu d’intégration n’est créé.
Les entrées vivent dans `STATE/git-file-previews/RANDOM_ID/` et sont retirées après succès/échec normal.
Un arrêt brutal peut les laisser sur place. Le nettoyage retire seulement les trois noms possédés et le dossier vide, sans suppression récursive des éléments inattendus.

## API et contexte stable {#api-and-stable-context}

L’API locale protégée et l’ABI C version 1 fournissent :

| Route POST | Corps |
| --- | --- |
| `/api/projects/ID/git-merge-review` | `{"peer":"PEER","branch":"main"}` |
| `/api/projects/ID/git-merge-file` | `{"peer":"PEER","branch":"main","path":"file.cpp","base":"LOCAL_OID","head":"PEER_OID","local_ref":"refs/heads/main","merge_base":"ANCESTOR_OID"}` |

`base` est le commit local, `head` celui importé, `merge_base` leur ancêtre.
Fixer les valeurs de l’aperçu pour une requête de fichier. Le contexte est vérifié après lecture ; les interfaces le fixent et ignorent les réponses périmées.
HEAD détaché utilise `local_ref:null` ou `--local-ref detached`. `apply` et `commit` sont refusés : aucune application n’existe sur ces routes.
La route séparée [git merge](doc:git-divergent-merge) intègre un résultat de dépôt examiné et accepte les choix de fichiers entiers local/pair pour les conflits texte pris en charge.

```sh
cysynck git merge-file PROJECT_ID PEER_ID BRANCH path.cpp \
  --base LOCAL_OID --head PEER_OID --local-ref refs/heads/main --merge-base ANCESTOR_OID
```
