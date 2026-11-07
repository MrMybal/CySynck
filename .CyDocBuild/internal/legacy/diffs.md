# Revue des différences

Le cœur C++ produit les comparaisons utilisées dans le desktop CyUI, le CLI et l'API.
La console Web affiche le patch texte et permet sa copie. Les fichiers du projet sont
ouverts en lecture ; aucune commande de comparaison ne fusionne ou ne supprime de version.
La [résolution explicite](resolutions.md) est une opération distincte, disponible après aperçu.

## Choisir une comparaison

```sh
cysynck diff PROJECT_ID notes.txt --snapshot SNAPSHOT_ID
cysynck compare PROJECT_ID notes.txt alternative.txt
cysynck conflicts PROJECT_ID
cysynck conflict-diff PROJECT_ID notes.sync-conflict-20261104-120000-ABCDEFG.txt
```

`diff` compare le snapshot au fichier de travail, ou au dernier snapshot si l'option est
absente. Il contrôle le hash de l'objet de sauvegarde lorsqu'il lit son contenu pour
l'aperçu. Un objet absent ou un checksum incorrect produit une erreur ; `verify` contrôle
la sauvegarde entière, y compris les fichiers qui dépassent la taille d'aperçu.

`compare` exige deux fichiers existants sous la racine du projet. Le côté gauche sert
de point de départ et le côté droit fournit le contenu cible. Le patch conserve le nom
du fichier de gauche : il ne décrit pas un renommage du fichier de droite.

`conflict-diff` compare l'original courant à la copie de conflit. Le patch cible le nom
original et représente donc le choix du contenu de la copie, sans l'appliquer. Un
original absent est représenté par `/dev/null` dans le patch de création.

## Copies Syncthing

La reconnaissance suit le [format documenté par Syncthing](https://docs.syncthing.net/users/syncing.html#conflicting-changes),
avec une date, une heure et l'identifiant court de l'appareil. Les fichiers sans extension,
les noms composés et les fichiers cachés sont pris en charge. La date reste celle du nom,
dont le fuseau n'est pas enregistré. Le nom indique une copie de conflit présumée ; il
ne prouve ni son origine ni l'identité de son auteur.

La recherche lève uniquement le glob par défaut `*.sync-conflict-*` pour cette vue.
Les autres exclusions du projet restent actives, y compris celles qui ciblent le nom
de l'original. `.git`, `.stfolder`, `.stversions` et `.stignore` sont toujours exclus de
la revue des conflits. Les liens et jonctions sont ignorés lors du parcours et refusés
lors d'une comparaison directe. Les noms de copies invalides figurent dans `errors`.
Au plus 10 000 copies valides sont retournées ; `count` et `truncated` signalent la limite.

Les copies sont conservées sur disque et continuent d'être des fichiers ordinaires pour
Syncthing. La vue n'est pas une liste des conflits de casse signalés par l'index Syncthing,
ni une fusion à trois versions, ni un résolveur des conflits Git. Le mode d'échange Git
conserve ses imports explicites et les outils Git habituels pour fusionner les branches.

## Patches et limites

Les patches texte utilisent les en-têtes Git et le format unifié, avec trois lignes de
contexte, des blocs séparés, `/dev/null` pour les créations et suppressions, et les marqueurs
de fin de fichier sans saut de ligne. Les chemins sont cités selon la syntaxe Git pour
préserver espaces, Unicode et caractères spéciaux. Les fichiers vides créés ou supprimés
sont également représentés. Les statistiques comptent les lignes ajoutées et supprimées.

La vérification automatisée applique ces patches avec un vrai Git configuré avec
`core.autocrlf=false`, puis compare les octets. Les conversions de fins de ligne, attributs
et paramètres du dépôt de destination peuvent influencer une application réelle. Les
patches décrivent le contenu ; ils ne transportent pas les ACL ou attributs étendus.

La lecture est limitée à 512 Kio par côté et 10 000 lignes pour l'aperçu texte. Les fichiers
binaires (octets NUL) ou non UTF-8 n'ont pas de patch texte. Leur SHA-256 est affiché si leur
taille a permis la lecture complète. Pour un fichier trop grand, l'égalité reste inconnue
(`identical: null`) et aucun hash n'est inventé.

Le calcul compare d'abord le préfixe et le suffixe communs. Si le bloc restant dépasse
le budget de deux millions de cellules, un remplacement complet de ce bloc produit un
patch correct, signalé par `approximate: true`, sans promettre un diff minimal. Le rendu
CyUI peut regrouper les lignes différemment du patch copié. Un changement de taille ou
de date pendant une lecture provoque un refus et demande une nouvelle comparaison.
La revue ne garantit pas un instantané transactionnel face à des écritures simultanées.
