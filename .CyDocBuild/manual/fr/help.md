---
id: help
title: Utiliser l’aide hors ligne
description: Ouvrir l’aide contextuelle, rechercher et conserver sa lecture sans serveur.
section: Démarrer
order: 5
parent: overview
sourceRevision: 1
---

# Utiliser l’aide hors ligne {#using-offline-help}

## Ouvrir le manuel {#open-the-manual}

Le dossier complet `Documentation/` accompagne l’application. Ouvrir directement `Documentation.html` dans un navigateur récent.
Conserver `DocData/` à côté. Aucun Node.js, serveur Web, accès Internet ou CDN n’est nécessaire à la lecture.
L’action **Aide / F1** du desktop ouvre le sujet correspondant à la vue courante dans la langue de l’interface.

```sh
cysynck docs
cysynck docs git-materialization --language fr
cysynck docs backups --section verifier-et-restaurer --print
```

`--print` renvoie l’URL locale correctement encodée sans lancer de navigateur, notamment sur les serveurs sans interface graphique.
`CYSYNCK_DOC_ROOT` permet de choisir un dossier de documentation complet existant lorsque l’emplacement installé habituel est indisponible.
La commande de terminal `cysynck help` continue d’afficher la syntaxe des commandes.

## Console Web et serveurs distants {#web-console-and-remote-servers}

Le dialogue d’aide Web propose une archive hors ligne et l’URL locale du sujet.
Sur un desktop local, **Ouvrir sur l’ordinateur serveur** demande au serveur en boucle locale d’ouvrir son manuel installé.
Un serveur distant ne peut pas lancer un navigateur sur votre poste ; télécharger et extraire l’archive complète, puis ouvrir `Documentation.html` localement.
Ajouter la route indiquée après le nom du fichier pour rejoindre le même sujet et la même section. Les navigateurs bloquent généralement la navigation directe de `http://` vers `file://`.
Le serveur API n’héberge pas le lecteur. Le point d’accès authentifié à l’archive transfère seulement une distribution complète à extraire localement.

## Naviguer et lire {#navigate-and-read}

Utiliser le sommaire latéral, la table des matières et les liens des titres. **Ctrl+K** ouvre la recherche ; les flèches et Entrée sélectionnent un résultat, Échap ferme la recherche.
La présentation étroite possède un menu de navigation. CyDoc fournit le focus clavier, le défilement tactile et des thèmes clair ou à contraste élevé.
Le thème sombre initial s’accorde avec le desktop ; le lecteur mémorise le thème choisi.
Le sélecteur anglais/français conserve les identifiants communs de page et de section. Une langue inconnue revient à l’anglais complet.
Aucune traduction incomplète n’est proposée. Les chemins et identifiants d’API restent identiques dans les deux langues.

CyDoc conserve favoris, notes, navigation et positions de lecture dans le stockage local du navigateur, sous `cysynck-help`.
La politique du navigateur peut restreindre ce stockage pour les fichiers locaux ; déplacer le manuel ou changer de navigateur peut créer un espace distinct.
Utiliser les favoris ou copier les URL des titres pour conserver une référence portable. Les schémas sont des SVG statiques ; cette édition ne charge aucun lecteur de média facultatif.
