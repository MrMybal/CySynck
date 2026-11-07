---
id: overview
title: CySynck
description: Synchroniser les projets, conserver des sauvegardes et examiner les changements.
section: Démarrer
order: 0
sourceRevision: 1
---

# CySynck {#cysynck}

CySynck regroupe la synchronisation, les sauvegardes versionnées, les comparaisons de fichiers et les échanges Git explicites par projet.
Le cœur C++20 est commun à la ligne de commande, au desktop CyUI et à la console Web/API.
Des builds desktop sont prévus pour Windows, macOS et Linux ; les serveurs Linux utilisent le CLI sans environnement graphique.

## Pour commencer {#start-here}

1. [Installer l’application](doc:install) et les outils externes nécessaires.
2. Suivre le [tutoriel du premier projet](doc:quickstart).
3. Choisir [le mode du projet](doc:projects) avant de partager son dossier.
4. Consulter [les sauvegardes et restaurations](doc:backups), [la revue des fichiers](doc:diffs) ou [l’échange Git](doc:git-setup).

## Un cœur, plusieurs interfaces {#one-core-several-interfaces}

```diagram
{"type":"flow","title":"Interfaces de CySynck et outils externes","nodes":[{"id":"ui","label":"Desktop CyUI / CLI / API Web","x":0,"y":0},{"id":"core","label":"Cœur partagé C++20","x":300,"y":0},{"id":"tools","label":"Syncthing / Git / WireGuard","x":600,"y":0}],"edges":[{"from":"ui","to":"core"},{"from":"core","to":"tools"}]}
```

Syncthing assure la synchronisation des fichiers. Git fournit les opérations de dépôt ; chaque participant conserve son dépôt local.
WireGuard fournit le tunnel VPN facultatif. Ces outils s’installent séparément. CySynck ne remplace pas leurs protocoles réseau.
L’interface Web originale de Syncthing reste accessible depuis la vue Moteur.

## Langues, aide et licence {#language-help-and-license}

L’anglais est la langue par défaut ; le français est la seconde langue complète. Le moteur, les clés d’API et les identifiants du CLI restent en anglais.
Les sélecteurs du desktop et du Web traduisent leur interface. Le manuel possède son propre sélecteur et conserve la section courante lors d’un changement de langue.
Utiliser **Aide / F1** dans le desktop ou `cysynck docs` pour ouvrir ce manuel local. Voir [l’aide hors ligne](doc:help) pour les serveurs.

CySynck est sous licence MIT. Les notices des dépendances accompagnent l’application et ce manuel.
Consulter [les fonctions disponibles et leurs limites](doc:availability) avant d’utiliser un workflow sur des données importantes.
Les liens de référence externes sont facultatifs et nécessitent Internet ; le manuel lui-même n’en a pas besoin.
