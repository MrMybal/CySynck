# Installation Windows 0.8

Le paquet contient le CLI, le desktop, le SDK et `Install-CySynck.ps1`. L'application et son
cœur restent en C++20 ; PowerShell sert uniquement au déploiement. Cette version fournit
un installateur par script, sans installeur EXE/MSI signé ni mise à jour téléchargée automatiquement.
L'entrée dans la liste « Applications installées » de Windows reste à ajouter ; le menu Démarrer
contient le raccourci de désinstallation.
Syncthing, Git et WireGuard restent des programmes externes à installer séparément.

## Installer

Extraire l'archive entière dans un dossier temporaire, puis exécuter à sa racine :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-CySynck.ps1
```

Le chemin proposé est `%LOCALAPPDATA%\Programs\CySynck`. Des raccourcis **CySynck** et
**Uninstall CySynck** sont ajoutés au menu Démarrer de l'utilisateur. Le script ne demande
pas de droits administrateur et ne change pas le PATH. Une autre destination est possible
avec `-Destination 'D:\Applications\CySynck'` ; la première installation exige un dossier vide.
Le dossier source extrait et la destination doivent être séparés.

Le démarrage à l'ouverture de session reste inchangé par l'installation. Il s'active ensuite
explicitement dans **Application → Inscrire au démarrage**, pour le profil choisi.

## Mettre à jour

Quitter CySynck et les applications utilisant son SDK. Extraire le nouveau paquet, puis relancer
son script avec la même destination. Les fichiers suivis sont contrôlés par SHA-256 avant
remplacement. Si un fichier a été modifié ou reste verrouillé, la mise à jour s'arrête.
Les copies précédentes sont conservées pendant le remplacement pour permettre un retour
aux fichiers d'origine en cas d'erreur. Si ce retour échoue, le dossier de sauvegarde reste
sur place avec un avertissement indiquant son chemin.

Les fichiers personnels non suivis ne sont pas remplacés. Un fichier non suivi qui occupe
le chemin d'un nouveau fichier du paquet bloque la mise à jour.

## Désinstaller

Utiliser le raccourci de désinstallation, ou :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\CySynck\Install-CySynck.ps1" -Action Uninstall
```

Pour une destination personnalisée, ajouter `-Destination` avec ce même chemin. Le script
retire les raccourcis de démarrage qui pointent vers cette installation, puis les raccourcis
du menu Démarrer qui lui appartiennent. Les fichiers du paquet non modifiés sont supprimés
individuellement ; les fichiers modifiés et non suivis sont conservés. Le dossier d'installation
ne disparaît que s'il devient vide. Aucune suppression récursive du dossier n'est effectuée.

Les profils, sauvegardes, identités et données reçues restent conservés. Le dossier d'état
habituel `%LOCALAPPDATA%\CySynck` est distinct du programme et ne peut pas servir de destination
d'installation. Le script ne modifie pas les installations de Syncthing, Git ou WireGuard.

## Options et essais

`-Action Status` décrit l'installation présente. `-NoShortcuts` évite la création des raccourcis
du menu Démarrer. `-Portable` évite ces raccourcis et le nettoyage des entrées de connexion lors
de la désinstallation ; conserver ce même mode lors des mises à jour. Il ne change pas le dossier
d'état : utiliser `--state` pour choisir où conserver les données d'une application portable.

Les essais de déploiement utilisent `-Portable` et une destination unique dans le workspace.
Ils vérifient l'installation, l'exécution du CLI installé, la mise à jour, le refus des fichiers
modifiés ou verrouillés, la désinstallation et la préservation des données. Ils ne créent pas
de raccourci personnel et ne changent pas le démarrage du compte :

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\deployment_tests.ps1 -Package dist
```
