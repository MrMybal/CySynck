# Résoudre un conflit et récupérer les sources

Les modes de synchronisation de fichiers et le mode sauvegarde proposent trois choix :
conserver la version courante, utiliser la copie Syncthing, ou fournir une fusion manuelle.
Le mode échange Git et les modes d'extensions ne sont pas concernés par cette opération.
La fusion n'est pas automatique et ne constitue pas un merge à trois versions.

## Préparer et appliquer

```sh
cysynck resolution-plan PROJECT_ID CONFLICT_PATH --choice conflict
cysynck pause PROJECT_ID
cysynck resolve-conflict PROJECT_ID CONFLICT_PATH --choice conflict --plan TOKEN_RETOURNE
cysynck resolutions PROJECT_ID
```

`current` garde le contenu courant, ou la suppression si l'original est absent.
`conflict` prend le contenu de la copie et peut recréer un original absent.
`merged` attend `--content TEXT` dans le CLI, ou `content` dans la requête JSON.
Les chemins avec espaces doivent être cités. Pour un texte important, utiliser l'éditeur
desktop ou l'API plutôt que la ligne de commande, dont la taille dépend du système.

L'aperçu calcule le SHA-256 complet des deux fichiers, jusqu'à 100 Mio chacun. La fusion
manuelle accepte au plus 512 Kio de texte UTF-8 sans NUL. Le CLI et l'API conservent les
octets fournis ; l'éditeur desktop et la zone Web utilisent des fins de ligne LF.
Le résultat reprend les permissions simples de l'original existant, sinon de la copie.

Le projet doit être en pause lors de l'application. Pour Syncthing, le dossier correspondant
doit également être en pause et désigner la même racine ; un moteur inaccessible bloque
l'opération. Le plan reste valide après la seule mise en pause, mais un changement de contenu,
taille, date ou permissions le périme. Le desktop et le Web fournissent aussi les empreintes
de la comparaison affichée : une modification antérieure à la préparation est ainsi refusée.
Le champ `modified_ns_exact` transporte la date en texte décimal pour éviter l'arrondi des
entiers 64 bits par JavaScript ; le champ numérique historique reste disponible.
Pour un fichier dépassant la limite d'aperçu, seul son état observable et sa date peuvent
être rapprochés de la comparaison ; le plan calcule ensuite son hash complet.

Avant application, l'espace est estimé avec une marge prudente. L'espace cumulé des copies
privées et du résultat temporaire doit être disponible sur chaque volume concerné, même
s'ils sont distincts. Il s'agit d'une estimation, sans réservation du disque.

## Ce qui est conservé

Chaque résolution possède un identifiant aléatoire et un journal dans :

```text
STATE/resolutions/PROJECT_ID/RESOLUTION_ID/
  resolution.json
  current.bin       (si l'original existait)
  conflict.bin
  merged.bin        (si une fusion manuelle était choisie)
```

Ces copies sont privées, relues et vérifiées avant déplacement des sources.
Elles sont indépendantes du stockage dédupliqué et de la rétention des snapshots.
Les fichiers sources sont ensuite renommés dans le même système de fichiers :

```text
PROJECT/.cysynck-recovery/RESOLUTION_ID/current
PROJECT/.cysynck-recovery/RESOLUTION_ID/conflict
```

Ce déplacement conserve les fichiers réels, notamment un inode encore ouvert sous POSIX.
Le dossier de récupération est protégé à sa création ; les fichiers déplacés gardent leurs
droits d'origine. Les copies dans l'état restent privées. Un marqueur réserve ce dossier
au profil et au projet : un dossier existant non reconnu n'est pas réutilisé.
Les fichiers de travail et le dossier de récupération doivent être sur le même système
de fichiers ; un montage interne distinct peut empêcher l'opération.

Le résultat est préparé avant de déplacer les sources, puis publié sans remplacer une
destination existante. Sous Windows, cette publication utilise
[MoveFileExW sans remplacement](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).
Sous Linux et macOS, elle exige la création exclusive d'un lien physique ; sa disponibilité
est testée avant le déplacement des sources. Un système de fichiers ne le permettant pas
refuse cette opération. Ces chemins POSIX restent à valider nativement sur ces plateformes.

`.cysynck-recovery` est toujours exclu des sauvegardes et de la revue des conflits.
La règle Syncthing est installée avant déplacement, en tête des
[règles d'exclusion](https://docs.syncthing.net/rest/db-ignores-get.html).
Le dossier ne doit pas être ajouté à Git ou partagé par un autre outil sans examiner son contenu.
Aucune copie de résolution n'est supprimée automatiquement dans cette version.

## Interruption et export

Si une opération échoue, CySynck tente de remettre chaque source à sa place lorsque le
chemin est libre. Il préserve un fichier apparu entre-temps. Si cette remise en place est
impossible, ou après un arrêt brutal dans la phase de déplacement, le projet est signalé
comme nécessitant un examen et une nouvelle résolution est bloquée.

```sh
cysynck resolutions PROJECT_ID
cysynck resolution-export PROJECT_ID RESOLUTION_ID /srv/recovered-copies
```

L'export vérifie toutes les copies privées avant de créer un nouveau dossier extérieur
aux projets et à l'état. Il y place `current/`, `conflict/`, éventuellement `merged/`,
avec les chemins de fichiers et le journal. Un problème de copie après création peut
laisser un export partiel ; les sources et copies privées ne sont pas retirées.
L'export restaure les contenus, pas les métadonnées originales.

Examiner aussi les fichiers réels dans `.cysynck-recovery/` : un éditeur POSIX qui gardait
un fichier ouvert peut encore y avoir écrit après le déplacement. Les copies privées
représentent les versions vérifiées lors de la préparation, pas ces écritures ultérieures.
Restaurer les fichiers souhaités ou vérifier l'état de travail, puis marquer le suivi comme examiné :

```sh
cysynck resolution-acknowledge PROJECT_ID RESOLUTION_ID
```

Cette commande exige un projet en pause. Elle enlève le diagnostic et conserve les copies ;
elle ne restaure ni ne valide les fichiers à la place de l'utilisateur. La synchronisation
reste à reprendre explicitement. En réception seule, les changements restent locaux selon
les règles de ce mode Syncthing ; CySynck ne transforme pas le projet en mode bidirectionnel.

La publication n'est pas une transaction multi-fichiers ni une protection absolue contre
une panne de matériel ou un acteur local capable de modifier les dossiers privés. Les
copies ne remplacent pas une sauvegarde indépendante du disque du projet et de l'état.
Les permissions simples sont reprises et la DACL est copiée sous Windows. Propriétaires,
groupes, ACL POSIX, attributs étendus et dates ne sont pas garantis pour le nouveau résultat.

## API et SDK C++

Les mêmes opérations passent par `Core::request` ou l'ABI C :

| Action POST sur `/api/projects/ID/` | Corps JSON |
| --- | --- |
| `resolution-plan` | `{ "path": "...", "choice": "current|conflict|merged", "content": "... si merged" }` |
| `resolve-conflict` | Même corps, avec `plan` renvoyé par l'aperçu |
| `resolutions` | `{}` |
| `resolution-export` | `{ "resolution": "id", "destination": "/new/directory" }` |
| `resolution-acknowledge` | `{ "resolution": "id" }` |

Les champs facultatifs `expected_current` et `expected_conflict` reprennent `before_info`
et `after_info` de `conflict-diff` pour lier la préparation à la revue affichée.
Le plan est une empreinte de cohérence, pas un mécanisme d'authentification supplémentaire.
L'API conserve les exigences habituelles de jeton Bearer et d'origine.
