---
id: cyrevision
title: Échange Git et LFS signé avec CyRevision
description: Connecter un transport CyRevision explicitement approuvé à un projet d’échange Git.
section: Git
order: 32
parent: git-lfs
sourceRevision: 1
---

# Échange Git et LFS signé avec CyRevision {#signed-cyrevision-exchange}

L’adaptateur C++ lit et écrit le transport Git/LFS signé de CyRevision 0.1.34
(commit de référence `9332a13d88e9c1ea939af8eef4f779ec52116fb3`). Il utilise un projet
d’échange Git existant et un dossier partagé distinct. Chaque participant conserve
son dépôt, ses fichiers de travail et son état privé. Syncthing transporte le dossier
d’échange. Voir [la préparation Git](doc:git-setup) et [le transport natif](doc:git-lfs).

Il nécessite Git et un exécutable OpenSSL externe prenant en charge ECDSA P-256.
Définir `CYSYNCK_OPENSSL` avec son chemin complet s’il n’est pas dans PATH. Windows
cherche aussi dans `mingw64/bin` et `usr/bin` de Git for Windows ; macOS vérifie
les emplacements habituels d’OpenSSL 3 sous Homebrew. .NET sert uniquement aux tests
de référence du développeur. Les outils Git LFS restent utiles pour créer les assets ;
CySynck lit les pointeurs canoniques et copie les objets vérifiés sans exécuter de
filtres ni de hooks.

## Connecter un projet {#connect-a-project}

Créer ou sélectionner un projet `git-bundles` avec un dépôt local existant et un
dossier d’échange séparé. Obtenir son **GUID de projet CyRevision**, distinct de
l’identifiant de projet CySynck, auprès de l’administrateur. Cette association est
immuable : utiliser un autre projet/profil CySynck pour un autre projet distant.

```sh
cysynck cyrevision identity --name "Poste Alice"
cysynck cyrevision configure game-team 12345678-1234-4234-8234-123456789abc
cysynck cyrevision status game-team
```

La première commande crée une clé privée P-256 dans le profil local et affiche
uniquement l’identité publique et son empreinte SHA-256 SPKI. La répéter préserve
l’identité et la clé existantes. Garder le profil privé et sauvegardé ; publier
seulement le JSON public. L’identifiant d’appareil Syncthing est une identité distincte.

Pour un **récepteur CyRevision Windows non corrigé**, ajouter
`--signature-format windows-legacy` à la configuration. Pour un récepteur Linux/macOS
non corrigé, utiliser `lf`, valeur par défaut. Le sérialiseur canonique indenté de
CyRevision utilise des fins de ligne dépendant du système : les anciens récepteurs
Windows et Unix ne peuvent pas tous vérifier le même nouveau message signé.
CySynck vérifie les deux formats. Un outil développeur prépare et teste une correction
CyRevision ciblée qui signe en LF et vérifie les deux formats, sans modifier le dépôt
de référence. Elle n’est pas appliquée automatiquement à une autre application installée.

## Approuver les appareils de publication {#approve-publishing-devices}

Obtenir le JSON public `DeviceIdentity` de l’autre appareil, puis vérifier son
empreinte avec cette personne par un canal distinct. Ne pas recopier une empreinte
non vérifiée depuis le même dossier partagé. Elle correspond au SHA-256 minuscule
du SPKI DER décodé.

```sh
cysynck cyrevision trust game-team peer-public.json --fingerprint SHA256_VERIFIE
cysynck cyrevision revoke game-team aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee
```

`peer-public.json` peut contenir l’identité seule ou la réponse publique CySynck
`{"identity": {...}, "fingerprint": "..."}`. L’approbation fixe le GUID d’appareil,
le GUID utilisateur et la clé publique pour ce projet. Révoquer explicitement une
identité avant de la remplacer. La révocation bloque les nouveaux imports et
préserve les branches de revue déjà importées. Elle n’efface pas les données et
ne révoque pas les certificats d’adhésion d’une autre application.

Les admissions et rôles des groupes restent gérés par CyRevision. Une identité
publique CySynck n’est pas automatiquement membre d’un groupe CyRevision existant.
La collection des appareils autorisés de CyRevision doit inclure cette identité
avant d’accepter sa publication. L’interopérabilité du transport ne contourne pas
l’admission et ne fournit pas toute l’interface d’adhésion et de rôles de CyRevision.

## Publier, importer et revoir {#publish-import-and-review}

```sh
cysynck cyrevision export game-team
cysynck cyrevision inbox game-team
cysynck cyrevision import game-team GUID_TRANSACTION
cysynck git branches game-team
```

L’export signe toutes les branches locales et, si l’option Git LFS du projet est
activée, publie leur historique LFS disponible avec un inventaire signé. Les objets
présents aux extrémités des branches ont la priorité courante ; les autres ont la
priorité archive. La politique LFS du récepteur CyRevision décide du téléchargement
des archives. Un objet local absent ou corrompu bloque l’export. Les métadonnées sont
publiées après le bundle et les objets. Les clés privées restent hors du dossier partagé.

L’import vérifie le projet, l’auteur approuvé, la signature P-256, le dossier de
transaction, le SHA-256 du bundle et la liste exacte des branches signées. Git lit
une copie privée du bundle. Les branches vont dans
`refs/remotes/cysynck/cr-GUID_APPAREIL_COMPLET_SANS_TIRETS/`. Leur transaction atomique
de mise à jour/suppression affecte uniquement les références de revue de cet appareil.
HEAD, l’index et les fichiers de travail sont préservés. Utiliser ensuite les outils
de [revue](doc:git-review), [d’extraction](doc:git-extraction) et
[d’intégration](doc:git-integration).

Les inventaires signés autorisent uniquement la copie des objets LFS référencés dans
l’historique importé. La taille et le SHA-256 sont revérifiés sur les octets copiés.
Les objets absents sont signalés par `lfs_deferred` ; répéter le même import après
leur arrivée pour compléter le cache. L’import ne développe pas les assets de travail.
Utiliser [la matérialisation](doc:git-materialization) après la revue et l’intégration.

Les séquences de publication non nulles doivent augmenter. Une publication ancienne
ou contradictoire est refusée ; une publication déjà importée à l’identique est
idempotente. Une modification externe des références de cet adaptateur provoque
un refus. La boîte de réception signale séparément les publications incomplètes,
non autorisées ou invalides.

## Reprendre un import interrompu {#recover-an-interrupted-import}

Une intention est enregistrée avant la transaction atomique de références Git.
Après un arrêt précédant l’enregistrement du reçu, inspecter l’appareil concerné :

```sh
cysynck cyrevision recover game-team GUID_APPAREIL
cysynck cyrevision recover game-team GUID_APPAREIL --apply true --plan JETON_COURANT
```

L’aperçu distingue les références inchangées, le résultat exactement appliqué et
les références modifiées ailleurs. Un aperçu frais et concordant permet d’enregistrer
l’import terminé ou de retirer une intention jamais appliquée. Il ne modifie ni les
références ni les fichiers de travail. Des références changées exigent une inspection ;
la reprise automatique ne les écrase pas.

## Interfaces, API et limites {#interfaces-api-and-limits}

Ouvrir **Échange Git → Échange CyRevision** dans le desktop ou l’interface Web.
Le panneau propose identité, configuration, approbation et révocation des appareils,
publication, import et reprise vérifiée. Son bouton d’aide ouvre cette page dans
la langue sélectionnée.

| Opération | Méthode et route JSON |
| --- | --- |
| Lire/créer l’identité publique | `GET`/`POST /api/cyrevision/identity` (`name` à la création) |
| Lire paramètres / réception signée | `GET /api/projects/ID/cyrevision-status` / `cyrevision-inbox` |
| Associer le projet | `POST /api/projects/ID/cyrevision-configure` (`project_id`, `signature_format`) |
| Approuver/révoquer l’appareil | `POST .../cyrevision-trust` (`identity`, `fingerprint`) / `cyrevision-revoke` (`device`) |
| Publier / importer | `POST .../cyrevision-export` / `cyrevision-import` (`transaction`) |
| Aperçu/application de reprise | `POST .../cyrevision-recover` (`device`, `apply` et `plan` facultatifs) |

Les métadonnées sont limitées à 1 Mio, 32 niveaux JSON, 1 024 branches et 4 096 objets
LFS. Les limites LFS natives sont 4 Gio par objet et 16 Gio par publication ; les
bundles sont limités à 16 Gio. Le dossier accepte au maximum 4 096 transactions et
1 024 entrées d’inventaire avant de demander un archivage. Le nettoyage des exports
natifs CySynck ne supprime pas les transactions CyRevision. L’adaptateur ne fournit
pas l’adhésion automatique, la réponse aux requêtes LFS, le nettoyage automatique
du transport ni la reprise partielle d’un bundle Git. Le test développeur de référence
vérifie les imports Git/LFS réels dans les deux sens ; voir [la disponibilité](doc:availability)
pour les validations de plateformes et de distribution.
