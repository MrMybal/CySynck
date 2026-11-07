---
id: inbox
title: Fichiers reçus
description: Lister les contenus privés reçus, vérifier SHA-256 et exporter une copie.
section: Réseau
order: 52
parent: network
sourceRevision: 1
---

# Fichiers reçus {#received-files}

Le récepteur direct conserve les fichiers dans l’état privé.
Desktop, Web, CLI et API C/C++ les listent, vérifient SHA-256 et exportent une copie.
La réception exige toujours pair de confiance, adresse VPN source correspondante et secret d’appairage.
Cette fonction ne change pas WireGuard et n’ajoute pas de traversée NAT.

## Lister, vérifier et exporter {#list-verify-and-export}

```sh
cysynck received list PROJECT_ID
cysynck received verify PROJECT_ID TRANSFER_ID
cysynck received export PROJECT_ID TRANSFER_ID /srv/new-export
```

Dans le desktop, ouvrir **Réseau → Ouvrir les fichiers reçus**. Sélectionner, vérifier, saisir un nouveau dossier séparé puis exporter.
Le Web propose **Fichiers reçus** après sélection d’un projet. Les deux interfaces sont anglais/français.
La destination doit être absente, séparée de tous les projets, échanges Git et profil, sans lien ni jonction.
Disposition obtenue :

```text
new-export/
  receipt.json
  payload/
    original-name.ext
```

Le reçu contient ID du transfert, projet, expéditeur, date de réception, nom, octets et SHA-256.
La copie se fait sans écrasement puis est revérifiée. L’export est privé à sa création ; la source reçue reste intacte.
Une erreur d’intégrité avant export ne crée aucun dossier.
Une erreur disque ultérieure peut laisser une sortie incomplète : l’examiner et choisir une nouvelle destination pour retenter.

## Catalogue et intégrité {#catalog-and-integrity}

Les contenus restent dans `STATE/inbox/PROJECT_ID/TRANSFER_ID.received`.
Les nouveaux reçus sont dans `registry.json.received_files`, indépendamment des 2 000 entrées d’historique du tchat.
La liste vérifie fichier régulier et taille, pas tous les hashes.
**Vérifier** et **Exporter** lisent le SHA-256 complet ; la disponibilité seule ne certifie pas l’intégrité.
Un rejeu exact vérifie le contenu stocké avant acquittement. Réutiliser l’ID avec autre contenu, nom ou expéditeur est refusé.
Les noms reçus sont des noms de fichiers simples portables UTF-8 sans périphérique Windows réservé.
Les chemins sont dérivés des IDs validés, pas acceptés depuis les champs du reçu.
Défauts : **4 Gio par profil**, **1 Gio par fichier**, réglables jusqu’à 64 Gio par quota et 4 Gio par fichier à reprise.
Les requêtes historiques restent à 100 Mio. Les partiels réservent leur taille entière dans le quota partagé ; voir [les transferts](doc:transfers).
Le catalogue durable accepte **10 000 reçus par projet**.
Aucune suppression automatique de contenu/reçu ; le nettoyage des objets de sauvegarde ne touche pas la réception.

## Anciens profils {#older-profiles}

Les reçus 0.7 proviennent des anciens messages entrants encore retenus si aucun reçu durable n’existe.
Le prochain fichier reçu migre ces reçus vers le catalogue persistant.
Si le message a déjà expiré, le reçu original ne peut être retrouvé ainsi ; le fichier apparaît **non indexé** et reste pour examen manuel.
Sans reçu il ne peut être exporté par l’opération vérifiée.
Reçus malformés/fichiers absents sont signalés sans masquer les valides.

## API {#api}

HTTP local authentifié et `Core::request` proposent ces POST :

| Route | Corps JSON |
| --- | --- |
| `/api/projects/ID/received-files` | `{}` |
| `/api/projects/ID/received-verify` | `{"transfer":"TRANSFER_ID"}` |
| `/api/projects/ID/received-export` | `{"transfer":"TRANSFER_ID","destination":"/srv/new-export"}` |

La liste contient `files`, `errors`, `unindexed`.
Chaque fichier valide fournit `available` et les champs de reçu `id`, `project`, `peer`, `created`, `name`, `bytes`, `sha256`.
Lister/exporter n’exige pas que l’expéditeur ou le tunnel soit encore en ligne.
