---
id: engine
title: Connecter Syncthing
description: Superviser un moteur isolé ou connecter une instance locale puis partager les projets.
section: Démarrer
parent: overview
order: 5
sourceRevision: 1
---

# Connecter Syncthing {#engine}

Installer Syncthing séparément ; son exécutable n’est pas inclus. CySynck peut superviser sa propre instance isolée ou connecter une instance locale existante.

## Instance gérée {#managed}

Dans **Moteur**, choisir **Instance gérée**, saisir le chemin de l’exécutable, enregistrer et démarrer.
CySynck crée identité, clé API et configuration propres dans le profil privé sans prendre le contrôle d’une autre instance.
Activer **Démarrer avec CySynck** pour lancer le moteur géré avec le desktop ou service.

```sh
cysynck engine configure --executable /opt/syncthing/syncthing --port 8385 --listen-port 22001 --auto-start true
cysynck engine run
```

`engine run` reste au premier plan ; Ctrl+C arrête son enfant. Desktop/service supervisent leur propre enfant.
Un crash déclenche une tentative de redémarrage différée. Les verrous empêchent deux contrôleurs CySynck et seul l’enfant possédé est arrêté.
Prévoir des ports distincts pour les moteurs indépendants. Le superviseur vérifie les options disponibles pour gérer les différences de CLI entre Syncthing 1.x/2.x.

## Instance locale existante {#existing}

Saisir l’URL de boucle locale et la clé REST dans le desktop local, ou fournir la clé par l’environnement du client :

```sh
cysynck engine connect http://127.0.0.1:8384
cysynck engine status
```

Le CLI local lit `CYSYNCK_CONNECT_API_KEY`. Garder la clé privée ; les routes de statut ne la révèlent pas.
Changer exécutable/connexion exige le CLI ou desktop local et est refusé par HTTP, `/api/cli` compris.
La connexion ne transfère pas la propriété du processus externe.

## Autoriser et partager {#share}

Utiliser le **même identifiant de projet** sur les appareils. Ajouter les appareils des deux côtés, enregistrer le projet et partager son dossier avec le pair autorisé.

```sh
cysynck engine add-device DEVICE_ID --name Laptop --address dynamic
cysynck register PROJECT_ID
cysynck engine share PROJECT_ID DEVICE_ID
cysynck sync PROJECT_ID
```

Sur un moteur externe, seuls les dossiers `cysynck-<project_id>` explicitement enregistrés sont gérés.
L’enregistrement crée les dossiers en pause pendant l’installation des exclusions nécessaires, puis applique l’état de pause du projet.
Il n’autorise aucun pair à lui seul. L’interface Web Syncthing originale reste accessible dans **Moteur**.
Un `.git` actif est refusé en synchronisation ordinaire ; choisir [l’échange Git](doc:git-lfs).
Exclusions de sauvegarde et exclusions de transfert Syncthing sont indépendantes. `sync` demande un scan ; Syncthing continue ensuite à transférer.
Une sauvegarde avant scan ne protège pas chaque réception. Le versionnage Syncthing protège les fichiers remplacés à distance ; les snapshots planifiés couvrent aussi les éditions locales.

## Examiner les échecs {#diagnostics}

Utiliser `engine status`, `engine devices`, `engine log` ou **Diagnostics** avec tests explicites.
Les journaux gérés tournent par rotation ; un moteur inaccessible bloque les opérations exigeant identité exacte du dossier ou pause confirmée.
Garder tout le profil privé et ne pas modifier la configuration moteur en concurrence avec CySynck.
