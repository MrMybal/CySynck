# Pont Unreal CySynck 0.2.0

[English](README.md)

L’extension d’exécution intègre le cœur C++ par l’ABI C version 1. Elle ne partage ni
types STL ni allocateurs avec Unreal et charge la bibliothèque de la plateforme depuis
son propre dossier `ThirdParty/CySynck/`. Le SDK et l’extension restent sous MIT ;
le moteur Unreal installé conserve ses propres conditions.

## Installer dans un projet

Compiler/installer d’abord CySynck pour le système et l’architecture cibles du moteur.
Depuis ce dossier :

```sh
cmake -DCYSYNCK_PREFIX=/chemin/absolu/cysynck-installe \
  -DUNREAL_PLUGIN_DIR=/chemin/absolu/VotreProjet/Plugins/CySynck \
  -P StageCySynck.cmake
```

La destination ne doit pas exister. Le script copie l’extension source, le cœur natif
adapté, les en-têtes du SDK, bibliothèques d’exécution et notices. Il copie aussi le manuel
hors ligne complet lorsqu’il est présent. Une destination existante est refusée pour
préserver les modifications du projet ; préparer une mise à jour dans un nouveau dossier
et l’examiner avant de remplacer l’extension existante. Le script est aussi distribué
dans `share/cysynck/integrations/unreal/` des paquets SDK.

Activer l’extension **CySynck** et compiler le projet. Ouvrir le manuel copié dans
`ThirdParty/CySynck/Documentation/Documentation.html#/fr/architecture/autres-applications-c-et-unreal`.
Garder les ressources `DocData/` ensemble. La lecture utilise les fichiers locaux sans serveur Web.
Les dépendances d’exécution préparent aussi les bibliothèques natives/CRT, notices,
licences et le manuel complet comme fichiers `NonUFS` dans un jeu cuit. `POST
/api/help/location` renvoie une URL de page/section validée à partir de la bibliothèque
SDK, sans ouvrir le navigateur ni modifier l’environnement de l’hôte. Voir la page
d’intégration du manuel SDK.

## Utilisation depuis Blueprint

1. Appeler **CySynck → Open** sur le thread de jeu avec un chemin d’état absolu explicite.
   Choisir un profil dédié, plutôt qu’un profil déjà ouvert par une autre application.
2. Utiliser **CySynck Request Async** pour les requêtes, par exemple `GET /api/overview`
   avec le corps `{}`. L’événement **Completed** renvoie `Success` et `ResultJson` sur le thread de jeu.
3. Examiner le JSON, dont les aperçus et jetons de plan. N’envoyer une écriture qu’après
   revue, avec les mêmes contrats API JSON que le CLI et l’application autonome.
4. Pour les sauvegardes planifiées, la supervision du moteur ou l’entretien VPN activé,
   soumettre `POST /api/maintenance/tick` avec `{}` via le nœud asynchrone environ toutes
   les 15 secondes. Le ticker distribue les fins de requêtes ; l’hôte planifie l’entretien.
5. Appeler **Close** à la fin. La requête active se termine avant fermeture de son handle natif ;
   les requêtes en attente reçoivent un échec explicite sans être exécutées.

Les requêtes s’exécutent en série, dans l’ordre FIFO, sur un thread de travail. La file
accepte au plus 64 requêtes en cours ou en attente ; tout dépassement échoue explicitement.
Le proxy asynchrone reste vivant jusqu’à la fin, puis libère sa référence d’instance de jeu/racine.
Si l’objet de contexte fourni a expiré, aucun événement Blueprint n’est diffusé.

L’appel **Request** synchrone reste disponible pour les appels volontairement bloquants.
Éviter les opérations coûteuses dans le tick du thread de jeu. **Close** peut bloquer
pendant la fin de l’opération active ; il n’interrompt pas une sauvegarde ou opération Git
en cours d’application. L’arrêt du module retire le ticker, attend le travailleur puis
libère la bibliothèque. Le rechargement dynamique du module est désactivé avec ce pont natif.

## Compiler et vérifier

Sous Windows, le script du dépôt prépare un projet de test isolé, le compile avec un
moteur déjà installé et lance les tests de l’extension en NullRHI :

```powershell
./scripts/test-unreal.ps1 -EngineRoot 'C:/UnrealEngine/UE_5.7' `
  -SdkPrefix 'D:/CySynck/dist' -WorkDirectory 'D:/CySynck/ue-smoke'
```

Lancer cette commande depuis la racine du dépôt CySynck. Le dossier de travail doit être
nouveau. Le script interdit les changements des sources du moteur et conserve journaux
de compilation et rapport d’automatisation JSON. Les tests couvrent l’ABI C réelle,
profils Unicode, callbacks FIFO asynchrones, durée de vie du proxy, saturation de la file
et fermeture avec des opérations actives/en attente.

Compilation et cinq tests d’automatisation réussis avec **Unreal Engine 5.7.4 et 5.8.3, Windows x64, Development Editor**.
Le **jeu Win64 Development empaqueté sous 5.8.3** passe aussi cinq tests après cuisson,
staging et archivage, avec 34 fichiers natifs/runtime/licences/manuel vérifiés par SHA-256.
Reproduire le parcours complet éditeur/jeu depuis la racine du dépôt :

```powershell
./scripts/test-unreal-packaged.ps1 -EngineRoot 'C:/UnrealEngine/UE_5.8' `
  -SdkPrefix 'D:/CySynck/dist' -WorkDirectory 'D:/CySynck/ue-packaged-smoke'
```

Le dossier de travail doit être nouveau. Journaux et DDC restent dans ce dossier ;
le test utilise une cuisson sur fichiers sans stockage Zen et lance le jeu caché avec
un délai maximal. NullRHI valide le pont natif et ses ressources, pas le rendu GPU.
Les hôtes Unreal Linux/macOS, autres versions du moteur et configurations Shipping
par plateforme nécessitent encore leurs propres compilations/exécutions. La CI native
vérifie que les paquets SDK contiennent une extension installable ; elle ne compile pas Unreal.
