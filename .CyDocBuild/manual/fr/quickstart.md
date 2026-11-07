---
id: quickstart
title: Votre premier projet
description: Créer et vérifier une sauvegarde locale, puis connecter la synchronisation.
section: Démarrer
order: 1
parent: overview
sourceRevision: 1
---

# Votre premier projet {#your-first-project}

## Créer un projet de sauvegarde {#create-a-backup-project}

Commencer par un petit dossier de fichiers ordinaires. Placer le profil d’état hors de tous les projets et dossiers d’échange.
Il contient les jetons privés, les sauvegardes et les fichiers reçus. Un profil sur le même disque ne constitue pas une copie indépendante contre une panne du disque.

```sh
cysynck project add Notes /chemin/absolu/notes --id notes --mode backup
cysynck project list
cysynck backup notes
cysynck snapshots notes
cysynck verify notes
```

Remplacer le chemin d’exemple par un dossier existant ; citer les chemins comportant des espaces. Sous Windows, utiliser par exemple `D:\Projects\Notes`.
Lire le champ `ok` de la vérification. Un code de sortie nul peut accompagner un rapport d’intégrité contenant `ok:false`.
Dans le desktop, choisir **Nouveau projet**, sélectionner le même dossier et le mode sauvegarde, puis ouvrir **Sauvegardes**.

## Restaurer une copie séparée {#restore-a-separate-copy}

Copier un identifiant dans la liste des snapshots. La destination doit être absente et extérieure aux projets, aux dossiers d’échange et au profil d’état.

```sh
cysynck restore-plan notes SNAPSHOT_ID /chemin/absolu/nouvelle-copie
cysynck restore notes SNAPSHOT_ID /chemin/absolu/nouvelle-copie
```

L’aperçu ne crée pas la destination. La restauration vérifie de nouveau le contenu et crée un nouveau dossier.
Examiner le résultat avant utilisation. Une copie interrompue par une erreur disque peut rester partielle ; le projet source est conservé.

## Ajouter la synchronisation {#add-synchronization}

Installer Syncthing séparément. Dans la vue **Moteur**, sélectionner un exécutable géré ou connecter une instance locale existante.
Le CLI local propose `engine configure` et `engine connect` ; consulter [la référence CLI](doc:cli) pour leurs options exactes.
Pour une instance existante, fournir sa clé REST privée dans `CYSYNCK_CONNECT_API_KEY`, plutôt que dans un argument conservé dans l’historique du terminal.

Créer un projet `sendreceive`, `sendonly` ou `receiveonly`, l’enregistrer, puis autoriser les appareils et partager le dossier des deux côtés.
L’enregistrement seul n’autorise aucun appareil distant. L’interface Web originale de Syncthing reste accessible pour ses réglages avancés.

```sh
cysynck project add Shared /chemin/absolu/partage --id shared --mode sendreceive
cysynck register shared
cysynck engine devices
cysynck sync shared
```

Ne jamais synchroniser un dossier `.git` actif. Choisir [l’échange Git](doc:git-lfs) pour un dépôt.
Adapter [les exclusions de sauvegarde](doc:backup-scope) et [la politique de stockage](doc:block-backups) aux gros projets.
