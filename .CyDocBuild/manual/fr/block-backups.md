---
id: block-backups
title: Stockage des sauvegardes par blocs
description: Choisir les fichiers entiers, les blocs fixes ou les blocs définis par le contenu.
section: Sauvegardes
order: 12
parent: backups
sourceRevision: 1
---

# Stockage des sauvegardes par blocs {#block-backup-storage-022}

Les sauvegardes réutilisent les portions inchangées des gros fichiers entre snapshots et projets du même profil.
La politique `auto` stocke les fichiers de **8 Mio à 256 Gio** en **blocs ordonnés de 4 Mio**, avec un dernier bloc plus court si nécessaire.
Les fichiers plus petits, plus grands et vides restent des objets entiers.
Ce stockage concerne les sauvegardes locales. Syncthing gère la synchronisation continue ; les transferts directs ont leur [protocole de reprise](doc:transfers).

## Choisir une politique {#choose-a-policy}

```sh
cysynck project add Assets /srv/assets --mode backup --storage auto
cysynck project set PROJECT_ID --storage whole
cysynck project set PROJECT_ID --storage auto
cysynck project set PROJECT_ID --storage cdc
cysynck backup PROJECT_ID
cysynck verify PROJECT_ID
```

Les anciens profils utilisent `auto` en l’absence de valeur. Les Paramètres, la création de projet et le Web proposent les trois politiques.
La propriété JSON `backup_storage` accepte `auto`, `whole` ou `cdc`. Un changement affecte les futurs snapshots ; les anciens conservent leurs objets et recettes.
Un fichier de deux blocs de 4 Mio et d’une fin de 97 octets écrit d’abord 8 Mio + 97 octets. Modifier un octet du second bloc écrit seulement 4 Mio à la capture suivante ; premier bloc et fin sont réutilisés.
L’ancienne version reste restaurable. Des blocs identiques sont partagés au sein d’un fichier et entre projets.
En mode `auto`, les frontières suivent des offsets fixes : une insertion au début peut décaler tous les blocs et réduire la réutilisation.
Chaque politique lit et hashe toujours la source. CDC peut réduire les données conservées après un décalage, sans promettre un parcours plus rapide ni une compression.

## Blocs définis par le contenu {#content-defined-storage-022}

Choisir explicitement `cdc` pour les fichiers de **8 Mio à 64 Gio**. Le cœur C++ découpe des blocs d’au moins 1 Mio et d’au plus 8 Mio, avec une fin éventuellement plus courte.
Les fichiers vides, plus petits ou plus grands restent entiers. Chaque recette est limitée à 65 536 blocs ; le plafond de fichier assure cette limite même avec des blocs minimaux.

Le découpeur déterministe `gear64-v1` utilise une empreinte non signée de 64 bits. Initialiser `h = 0` ; pour chaque octet `b`, calculer `h = (h << 1) + G[b]` modulo 2^64.
Après au moins 1 Mio, couper si les 22 bits faibles valent zéro, ou obligatoirement à 8 Mio. Remettre `h` à zéro après chaque coupe.
L’échelle du masque de 4 Mio ne promet pas une longueur moyenne exacte. Publier la fin non vide ; les fichiers vides n’ont pas de recette.
`G` comporte 256 entrées non signées, produites avec la graine `0x435953594e434b31`.
Pour chaque entrée, ajouter `0x9e3779b97f4a7c15` à la graine, puis mélanger par xor/décalages à droite successifs de 30, 27 et 31 bits ;
après les deux premiers décalages, multiplier respectivement par `0xbf58476d1ce4e5b9` et `0x94d049bb133111eb`. Tous les intermédiaires sont modulo 2^64.
Cette définition versionnée est propre à CySynck et n’ajoute aucun outil ni runtime.

Après insertion/suppression, les frontières peuvent rejoindre celles du contenu antérieur. Le gain dépend des octets et de la position ; des données répétitives peuvent forcer des blocs maximaux sans gain notable.
Le découpeur choisit seulement les frontières. SHA-256 identifie et vérifie les octets des blocs et du fichier reconstruit.
Les entrées `storage:"blocks-cdc-v1"` reprennent la liste ordonnée `{sha256,size}` ci-dessous. Les tailles doivent couvrir exactement le fichier ;
les blocs non finaux font 1–8 Mio et le dernier 1 octet–8 Mio. Le lecteur vérifie ces limites puis les octets, sans devoir recalculer le découpage.
Le buffer contient au plus un bloc de 8 Mio, plus les buffers de lecture/hash, indépendamment de la taille totale.
CDC et blocs fixes partagent `STATE/blocks/SHA256`. Les recettes restent attachées aux snapshots. Les lecteurs antérieurs à 0.22 refusent CDC : utiliser 0.22+ pour restaurer ou nettoyer un profil qui en contient.

## Format et vérification {#format-and-verification}

Les manifests version 2 gardent SHA-256 complet, taille et permissions simples de chaque fichier. Les gros fichiers ajoutent une recette ordonnée :

```json
{
  "sha256": "FULL_FILE_SHA256",
  "size": 8388705,
  "permissions": 420,
  "storage": "blocks-v1",
  "blocks": [
    {"sha256": "FIRST_BLOCK_SHA256", "size": 4194304},
    {"sha256": "SECOND_BLOCK_SHA256", "size": 4194304},
    {"sha256": "TAIL_SHA256", "size": 97}
  ]
}
```

Les objets entiers restent dans `STATE/objects/SHA256`, les blocs dans `STATE/blocks/SHA256`.
Les vrais digests sont 64 caractères hexadécimaux minuscules. Limites : 65 536 blocs par fichier, manifest de 64 Mio.
Formats inconnus, dispositions/tailles invalides, chemins dangereux et résumés incohérents sont refusés avant rétention ou nettoyage.
La capture stocke atomiquement chaque bloc et publie le manifest après contrôle de stabilité de taille/date. Un fichier changeant est retenté au plus trois fois.
Une capture interrompue peut laisser des blocs sans référence pour le nettoyage explicite. Une nouvelle capture authentique répare atomiquement un bloc corrompu, y compris pour les anciens snapshots.

La vérification contrôle les tailles, chaque digest de bloc et le digest de la concaténation complète : elle détecte aussi les blocs réordonnés pourtant individuellement valides.
Les recettes répétées partagent leurs résultats ; le fichier entier n’est pas chargé en mémoire. Ce contrôle d’intégrité n’est pas une signature authentifiée.
La restauration vérifie le snapshot avant de créer sa destination, puis lit et vérifie chaque fichier de nouveau. Les sorties sont créées exclusivement et contrôlées avant application des permissions.
Les anciens manifests sans marqueur restent compatibles. Les sauvegardes à blocs fixes nécessitent 0.11+ pour leur restauration.
Les [exclusions et sorties partielles](doc:backups) s’appliquent toujours. Le diff reste limité à 512 Kio : au-delà, `expected_sha256` désigne le digest du manifest, `sha256` reste nul dans l’aperçu. Utiliser `verify` pour contrôler l’intégrité complète.

## Statistiques et nettoyage {#statistics-and-cleanup}

`block_storage` contient `chunked_files`, `block_size`, `new_blocks`, `reused_blocks`, `new_block_bytes`, `reused_block_bytes`.
Les compteurs décrivent les écritures/réutilisations de la capture réussie, réparations comprises, pas la croissance physique du disque. CyUI et Web les affichent.
`content_defined_files` compte les versions CDC. `block_size` reste l’échelle de masque 4 Mio pour CDC et la taille exacte des blocs fixes ; chaque vraie taille CDC figure dans la recette.
`storage status` additionne les deux espaces d’objets et fournit :

| Champ | Signification |
| --- | --- |
| `block_objects`, `block_stored_bytes` | Fichiers de blocs reconnus et somme de leurs tailles |
| `chunked_files` | Versions de fichiers à recettes de blocs dans les snapshots conservés |
| `content_defined_files` | Versions de fichiers à recettes CDC |
| `referenced_bytes` | Octets des objets reconnus encore référencés |
| `deduplicated_bytes` | Octets logiques conservés moins octets physiques référencés |
| `deduplication_complete` | Indique si les objets absents/erreurs empêchent ce calcul |

Manifests, surcoût du système de fichiers, réception et copies de récupération indépendantes sont exclus.
L’inventaire ne hashe pas les objets ; utiliser `verify`. Si le catalogue est incomplet, le gain vaut zéro avec `deduplication_complete:false`.
Le nettoyage reste explicite avec un nouveau jeton. Il suit tous les manifests conservés, projets non enregistrés et formats mélangés compris.
Fichiers inconnus/temporaires et blocs référencés sont préservés ; les espaces liés sont refusés et les dossiers inattendus ne sont pas parcourus récursivement.
Capture, restauration et collecte partagent les verrous de processus/disque du profil.
