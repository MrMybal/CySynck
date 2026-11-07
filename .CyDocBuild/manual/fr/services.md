---
id: services
title: Préparer un service utilisateur sans interface
description: Exporter une unité systemd ou un LaunchAgent sans installation automatique.
section: Exploitation
order: 60
sourceRevision: 1
---

# Préparer un service utilisateur sans interface {#prepare-a-headless-user-service}

Les vues **Services** du desktop/Web préparent les mêmes configurations que le CLI et l’API. Modifier une entrée périme l’aperçu.
Les destinations d’export appartiennent à la machine du cœur/serveur, pas au système de fichiers du navigateur.
CySynck prépare une unité utilisateur systemd Linux ou un LaunchAgent macOS depuis n’importe quel hôte, y compris Windows.
La préparation/export n’installe, n’active et ne démarre aucun service. Le démarrage du desktop Windows reste disponible par `desktop startup`.
Choisir les chemins absolus du CLI installé et du profil **sur la cible**. Garder `bin`, `lib`, `share`, `Documentation/` et l’archive d’aide ensemble.
Configurer et tester le profil sur place avec `doctor --probe true`. Activer l’auto-démarrage Syncthing géré si nécessaire et prévoir un port API distinct par profil actif.

```sh
cysynck service plan --platform linux --executable /opt/cysynck/bin/cysynck \
  --target-state /srv/cysynck/team --port 8484
cysynck service export /tmp/team-service --platform linux \
  --executable /opt/cysynck/bin/cysynck --target-state /srv/cysynck/team \
  --port 8484 --plan TOKEN_FROM_THE_PREVIEW
```

Remplacer `linux` par `macos` pour un LaunchAgent. Sur POSIX, omettre `--target-state` utilise le profil `--state` du CLI ; sous Windows, fournir explicitement le chemin POSIX cible.
Les chemins doivent être absolus, sans composant point ni caractère de contrôle. Systemd refuse aussi guillemets, antislashs et jokers dans le chemin de l’exécutable ; ces valeurs sont refusées avant aperçu.
Espaces, pourcentages et dollars littéraux sont acceptés. Le port est un entier 1024–65535 ; l’écoute reste sur `127.0.0.1`, avec tunnel SSH pour l’administration distante.
L’aperçu contient le texte exact, les arguments, le nom de fichier et les instructions. Le jeton lie plateforme, chemins, port et contenu ; une modification exige un nouvel aperçu.
Le nom stable identifie le profil cible ; changer l’exécutable ou le port conserve cette identité.
L’export réserve un nouveau dossier local dont le parent existe, hors des projets actifs, échanges et profil courant.
Il écrit le service examiné, un `README.md` d’installation/retrait propre à la cible et `service-export.json` avec SHA-256.
Une destination existante n’est jamais écrasée. Une erreur disque conserve la sortie partielle. Ni clés ni données des projets/sauvegardes ne sont exportées.
Lire le README sur la cible avant ses commandes. Linux utilise le gestionnaire systemd de l’utilisateur ; la persistance après déconnexion dépend de la politique de l’administrateur.
Les LaunchAgents tournent dans la session connectée. Desktop et serveur ne doivent pas contrôler simultanément le même profil : arrêter le propriétaire actuel.
Systemd journalise dans le journal système et arrête le groupe de processus, moteur géré compris.
Le LaunchAgent écrit stdout/stderr sous l’état cible, que les instructions créent avant le démarrage ; prévoir leur rotation.
Les deux formats relancent après échec et imposent un umask privé.
API : POST `/api/service/plan` et `/api/service/export`, avec `platform`, `executable`, `target_state` facultatif et `port`.
L’export exige aussi `plan` et `destination` locale. L’ABI C fournit ces opérations par sa fonction de requête générique.
L’activation réelle sous systemd/launchd doit être validée sur les systèmes cibles.
Références facultatives : [commandes systemd](https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html#Command%20lines),
[échappement systemd](https://www.freedesktop.org/software/systemd/man/latest/systemd.syntax.html#Quoting),
[LaunchAgents Apple](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html).
