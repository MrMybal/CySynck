---
id: projects
title: Projets et modes
description: Choisir la synchronisation, les sauvegardes, l’échange Git ou un mode d’extension de confiance.
section: Démarrer
order: 4
parent: overview
sourceRevision: 1
---

# Projets et modes {#projects-and-modes}

## Choisir un mode {#choose-a-mode}

| Mode | Utilisation |
| --- | --- |
| `sendreceive` | Dossier Syncthing qui reçoit et envoie les changements |
| `sendonly` | Dossier Syncthing en envoi seul |
| `receiveonly` | Dossier Syncthing en réception seule ; les changements locaux suivent les règles de ce mode Syncthing |
| `backup` | Snapshots locaux sans dossier Syncthing |
| `git-bundles` | Synchroniser un dossier d’échange séparé contenant les bundles Git et les objets LFS facultatifs |
| Identifiant d’un mode d’extension | Exécuter un mode fourni par une extension native de confiance chargée explicitement |

Chaque projet possède un identifiant stable, un nom d’affichage et une racine absolue. Le dossier d’échange Git doit rester séparé du dépôt et du profil.
Renommer le projet ne change pas son identifiant dans les chemins d’API. Utiliser des profils `--state` indépendants pour des comptes ou environnements distincts.

## Configurer les sauvegardes {#configure-backups}

Les paramètres comprennent `backup_before_sync`, l’intervalle `backup_interval_minutes`, le nombre de manifests conservés `backup_keep`,
`backup_storage` et les exclusions de sauvegarde. Le planificateur fonctionne tant que le desktop ou le serveur API normal reste actif.
Une commande CLI ponctuelle ne laisse pas tourner de planificateur après sa fermeture. Les projets en pause n’exécutent pas les actions de synchronisation.
La rétention retire les anciens manifests après un snapshot réussi ; les contenus inutilisés nécessitent un [nettoyage explicite](doc:backups).

```sh
cysynck project set PROJECT_ID --interval 30 --keep 20
cysynck project set PROJECT_ID --storage auto
cysynck pause PROJECT_ID
cysynck resume PROJECT_ID
```

## Revue et paramètres {#review-and-settings}

Sélectionner un projet dans le panneau latéral avant d’ouvrir Différences, Conflits, Sauvegardes, Échange Git ou Paramètres.
Les vues globales comme Stockage, Diagnostics et Extensions concernent le profil. L’enregistrement du dossier dans le moteur peut être nécessaire avant une synchronisation.
Configurer [les options des extensions](doc:plugins), [la politique Git LFS](doc:git-lfs) et [le périmètre des sauvegardes](doc:backup-scope) dans leurs vues et aperçus dédiés.
Consulter [la liste des commandes CLI](doc:cli) pour les options disponibles plutôt que modifier le registre à la main.
