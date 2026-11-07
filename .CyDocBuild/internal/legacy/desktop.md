# Desktop 0.8

La vue **Réseau → Ouvrir les fichiers reçus** liste les transferts directs conservés dans
l'état privé. Elle permet une vérification SHA-256 et un export vers un nouveau dossier
séparé. Le catalogue reste disponible après rétention de l'historique du tchat.
Voir [la boîte de réception](inbox.md) pour les limites et les anciens profils.

L'interface et la logique de session sont en C++20 et utilisent le même cœur que le CLI.
Le backend de la barre de menus macOS utilise un petit pont Objective-C++ vers AppKit ;
il reste séparé du cœur, du serveur et du SDK Unreal.

## Différences et conflits

La vue **Différences** propose la source **Sauvegarde** ou **Deux fichiers du projet**.
Dans ce second mode, saisir les deux chemins relatifs et choisir **Comparer**. La vue
affiche les statistiques, le choix côte à côte, **Copier le patch** et les détails des
fichiers. Désactiver la vue côte à côte pour lire les changements dans une seule colonne.

La vue **Conflits** lance une recherche à son ouverture puis permet de la renouveler.
Choisir une copie dans la liste affiche le fichier courant à gauche et la copie Syncthing
à droite. Un original absent est traité comme une création. Le nom de l'appareil est
son identifiant court ; la date affichée provient du nom du fichier, sans conversion
de fuseau horaire. La section Diagnostic regroupe les copies non reconnues ou inaccessibles.

La revue conserve les deux fichiers. Le patch exprime le passage du fichier de gauche
au contenu de droite. Le bloc **Résoudre le conflit** propose la version courante,
la copie de conflit ou une fusion manuelle. L'éditeur manuel utilise UTF-8 avec fins
de ligne LF. Préparer le choix, mettre le projet en pause, puis appliquer l'aperçu encore
valide. Tout changement depuis la comparaison ou la préparation provoque un refus.

L'application ouvre ensuite **Récupération**, également accessible depuis **Historique
des résolutions** dans Conflits. Les deux versions peuvent être exportées vers un nouveau
dossier séparé. Le projet reste en pause : reprendre explicitement après vérification.
Une interruption est signalée dans la vue d'ensemble et conduit vers cet historique.
Après récupération ou examen des fichiers de travail, **Marquer la récupération comme
examinée** libère le diagnostic et permet une nouvelle résolution, sans supprimer les copies.
Voir [le format des différences](diffs.md) et [les limites de récupération](resolutions.md).

## Intégrité et stockage

Dans **Sauvegardes**, sélectionner un snapshot puis **Vérifier la sauvegarde** pour contrôler
ses objets SHA-256. Saisir une destination séparée et choisir **Préparer la restauration** :
l'application affiche les fichiers et leur taille. Le bouton de restauration s'active après
un aperçu valide pour cette destination ; le cœur refait les contrôles lors de l'exécution.
Un changement de sauvegarde ou de destination exige un nouvel aperçu.

La vue globale **Stockage** propose **Analyser le stockage**, puis affiche l'espace occupé,
le nombre de versions, les objets manquants et l'espace récupérable. **Libérer l'espace
inutilisé** applique ce bilan si le catalogue n'a pas changé. Si une sauvegarde ou une autre
opération change le stockage entre-temps, relancer l'analyse. Les manifests illisibles
bloquent le nettoyage, et les fichiers non reconnus restent conservés.

Ces opérations utilisent le même cœur C++ que le CLI et le serveur et s'exécutent en arrière-plan
de la fenêtre. Voir [les limites et commandes des sauvegardes](backups.md).

## Fermer, masquer et quitter

La vue Application propose deux préférences enregistrées par profil :

- Fermer la fenêtre vers la zone de notification (`ui_close_to_tray`, activé par défaut).
- Démarrer l'application en arrière-plan (`ui_start_hidden`, désactivé par défaut).

Une fenêtre ne se masque que si un accès par l'icône est disponible. Si ce support disparaît
et que le backend le détecte, la fenêtre est réaffichée. Sans zone de notification, la croix
ferme l'application. Le menu « Quitter CySynck », le bouton de la vue Application et Ctrl+Q
(Cmd+Q lorsque la fenêtre est active sous macOS) quittent complètement l'application.
Une opération déjà en cours est attendue avant la libération du cœur et de son moteur.

Le masquage ne suspend pas les transferts Syncthing, les sauvegardes planifiées ou les actions
du menu. Il suspend le rendu OpenGL. La boucle de présentation traite les commandes avec
une attente de 100 ms entre les événements, tandis que le scheduler reste dans son propre thread.
Le menu « Sauvegarder tous les projets » traite les projets actifs, conserve les erreurs par
projet et continue les autres sauvegardes. Les résultats se trouvent dans la vue Application ;
les snapshots réussis apparaissent aussi dans Sauvegardes et Activité.

## Session unique par profil

`desktop.lock` reste détenu jusqu'à l'arrêt du cœur. Un second processus ne crée pas de fenêtre
et demande au propriétaire de réafficher la sienne. Les demandes `show` passent par de petits
fichiers dans `desktop-requests/`, avec des noms aléatoires, dans l'état privé de l'utilisateur.
Elles sont consommées une fois ; les autres actions et les demandes de plus de 60 secondes
sont ignorées. Ce canal n'accepte aucune commande CLI, aucun chemin de fichier à ouvrir
et aucune instruction d'arrêt. Aucun port réseau supplémentaire n'est exposé.

Le profil `--state` permet toujours de lancer plusieurs desktops indépendants. Le serveur
CLI ne prend pas le verrou desktop ; il conserve sa propre propriété du moteur Syncthing.

## Plateformes

- Windows : `Shell_NotifyIconW`, menu natif et icône issue de la couleur d'accent CyUI.
  L'icône est réinscrite lorsque Windows annonce le redémarrage d'Explorer. Le programme
  utilise le sous-système graphique et n'ouvre pas de console supplémentaire.
- macOS : `NSStatusBar` et menu AppKit. Compilation et comportement à valider sur macOS.
- Linux : GTK3 et Ayatana AppIndicator facultatifs, détectés par pkg-config à la compilation.
  Le masquage attend une connexion au panneau StatusNotifier. Sans bibliothèque ou panneau,
  l'interface fonctionne avec une fenêtre visible. Certains environnements GNOME nécessitent
  une extension pour les indicateurs. Ce backend reste à valider sur un desktop Linux réel.
- Serveurs Linux : `CYSYNCK_DESKTOP=OFF` conserve un build sans dépendance graphique.

Le lancement masqué seul ne configure pas le démarrage du système. La section **Démarrage
à l'ouverture de session** inscrit explicitement le profil pour la prochaine session graphique.
Chaque profil a sa propre entrée et ses arguments ; les projets d'un même profil utilisent
la même application. Le statut décrit l'entrée enregistrée, pas une garantie d'exécution
par le système ou d'autorisation par sa politique de connexion.

Windows utilise un raccourci dans le dossier Démarrage de l'utilisateur. Linux produit
un fichier `.desktop` dans `$XDG_CONFIG_HOME/autostart` (ou `~/.config/autostart`). macOS
produit un LaunchAgent dans `~/Library/LaunchAgents`, limité à la session Aqua et sans
redémarrage automatique après « Quitter ». Les retraits concernent les prochaines connexions
et ne terminent pas l'application en cours. Aucun service système n'est créé.

Les mêmes actions sont disponibles avec `cysynck desktop startup status|enable|disable`.
Ces commandes et les boutons de l'application sont locaux ; le serveur Web les refuse.
La commande Windows `desktop startup cleanup` retire les inscriptions de tous les profils
qui pointent vers cette installation précise, sans supprimer leurs données. Elle est utilisée
par la désinstallation. Les inscriptions d'une autre installation restent indépendantes.

Les chemins sont absolus. L'inscription conserve les extensions explicitement chargées et
n'enregistre pas les secrets d'environnement. Après déplacement du logiciel ou modification
de la liste d'extensions, utiliser « Mettre à jour l'inscription ». Un fichier étranger ou un
lien occupant le nom prévu n'est ni remplacé ni supprimé automatiquement.

Les captures de contrôle `--capture` se font sans icône de notification et rendent hors écran.

## Vérification

Les tests de session couvrent le verrou par profil, la libération du verrou, les demandes
à usage unique, le rejet des autres actions et les préférences persistées. Sous Windows,
avec `CYSYNCK_TEST_SYNCTHING`, un test supplémentaire pilote une application isolée :
lancement masqué, sauvegarde planifiée, moteur réel, second lancement, fermeture vers
l'icône, sauvegarde depuis son menu, arrêt et repli sans icône. Les données sont placées
dans `build/desktop-test-data` et ne changent pas les préférences du profil personnel.

Les tests de démarrage utilisent leurs propres dossiers `startup-test-data` : création,
mise à jour, retrait par profil, préservation des entrées étrangères, nettoyage par installation,
encodage XML et arguments avec espaces, accents et caractères réservés. Sous Windows,
un raccourci réel lance un programme de test et les arguments reçus sont comparés aux arguments
attendus. Avec GIO disponible sous Linux, le fichier `.desktop` est exécuté de la même manière.
La connexion et déconnexion réelles d'un compte ne sont pas déclenchées par ces tests.

Références : [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw),
[NSStatusBar](https://developer.apple.com/documentation/appkit/nsstatusbar),
[Ayatana AppIndicator](https://github.com/AyatanaIndicators/libayatana-appindicator).

Formats de démarrage : [raccourcis Windows](https://learn.microsoft.com/en-us/windows/win32/shell/links),
[autostart de session freedesktop](https://specifications.freedesktop.org/autostart-spec/latest/),
[arguments Exec des fichiers desktop](https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html),
[LaunchAgents Apple](https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html).
