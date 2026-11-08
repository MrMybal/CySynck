# CySynck — Notes de version

[Retour au README](README.fr.md) · [English](VERSION.md)

**Version des sources du cœur, du CLI et de l’application native : 0.41.0**<br>
**Version des sources du pont Unreal : 0.2.0**<br>
**Statut : EN DÉVELOPPEMENT**<br>
**Binaires de l’application publiés : aucun**

Ces notes décrivent les sources actuelles, pas une version binaire publiée. L’ABI C reste en version 1. La documentation fournie utilise le lecteur CyDoc 0.1.0 ; cette version de dépendance est distincte de celle de CySynck.

## Changements de la version 0.41.0

- Liaison des clients et récepteurs TCP directs à des interfaces WireGuard vérifiées sous Linux, Windows et macOS, avec refus des adaptateurs ordinaires et identités périmées. Les vrais tests noyau Linux passent pour interfaces ordinaires/usurpées, routes physiques superposées et hôte sans privilèges. Les tests TCP Windows locaux et d’héritage de liaison des sockets acceptés passent ; les essais des vrais pilotes Windows/macOS sont intégrés et attendent les résultats de la CI native.
- Conservation des compteurs contre le rejeu après révocation/nouvel appairage. Entretien des baux PCP/NAT-PMP explicitement activés et publication signée des véritables adresses du routeur, avec tests Linux indépendants de deux routeurs et de préservation d’une autre redirection.
- Ajout des essais reproductibles d’amorçage des profils publics avec de vrais moteurs, des pilotes officiels en CI et des instructions de mise à jour des paquets portables. La portée des validations et les prérequis externes de signature restent précisés ci-dessous.

## Travaux antérieurs et base de la version 0.40.0

- Noms de tunnels WireGuard propres à chaque profil, contrôle des clés et de l’appartenance des services Windows avant action, résolution des noms macOS et préservation des tunnels étrangers/historiques. Modèles isolés d’outils/services et vrai test Linux en espaces réseau réussis ; les pilotes Windows/macOS restent à vérifier ; le vrai test NAT Linux à deux routeurs réussit.
- Publications d’adresses VPN signées à durée limitée, confirmation indépendante de l’empreinte, compteurs privés contre le rejeu, indices LAN/IPv6 directs de Syncthing, actualisation automatique optionnelle et révocation active par projet. Les configurations privées sont mises à jour pour empêcher le retour d’un pair révoqué au redémarrage du service. Les contrôles locaux explicites PCP/NAT-PMP entretiennent les baux privés, lient les demandes à l’adresse locale consentie, publient les endpoints réellement attribués et limitent le nettoyage au port/nonce possédé. Les tests de protocole, cycle de bail et publication Core passent, ainsi que le vrai transfert PCP/NAT-PMP via deux routeurs Linux indépendants.
- Génération, signature et vérification P-256 dans les chemins de profil français/chinois sans modification globale de l’environnement OpenSSL.
- Export des règles AppArmor limitées aux configurations VPN privées Linux, avec un test en mode d’application qui restaure son inclusion temporaire. Les vrais échanges Linux passent pour clés, tchat, fichiers vérifiés, révocation/redémarrage et préservation d’une interface étrangère. Les remplacements POSIX privilégiés conservent le propriétaire existant pour éviter un registre devenu propriété de root.
- Test optionnel sur 2,25 Gio réels traités par flux : transfert interrompu/repris malgré l’édition de la source, insertion en début de sauvegarde CDC et restauration complète. L’essai Windows enregistré réutilise 99,91 % des octets de blocs et atteint environ 36,3 Mio de mémoire résidente ; mesures et portée sont documentées, séparément de la validation du routage WireGuard.
- Transport Git/LFS signé CyRevision 0.1.34 avec identités approuvées par empreinte, séquences d’import croissantes, références de revue atomiques et reprise après interruption, depuis CLI/desktop/Web et l’aide bilingue. Les imports réels .NET/C++ passent dans les deux sens ; l’admission reste gérée par CyRevision. Les outils de test isolent une correction de ses signatures dépendant des fins de ligne du système.
- Ajout d’un repli OpenGL historique, des diagnostics GLFW et de vérifications réelles du framebuffer des applications empaquetées, dont les contextes macOS sans GPU.
- Pont Unreal 0.2.0 avec requêtes FIFO sur un travailleur, événements Blueprint sur le thread de jeu, file bornée et fermeture/arrêt déterministes. Le script de préparation préserve les extensions existantes ; les tests réels dans Unreal 5.7.4 et 5.8.3 Windows Editor passent.
- Cinq vrais tests du pont réussis dans un jeu Unreal 5.8.3 Win64 Development cuit/préparé/archivé, avec bibliothèques natives/CRT, licences et documentation locale livrées comme fichiers accessibles. Le script reproductible vérifie 34 empreintes et attend la fin des tests du jeu caché. Les hôtes ABI C peuvent résoudre l’aide locale et demander l’entretien périodique sans nouvelle ABI binaire.
- Export des assets Git LFS préservés par les reçus de restauration des pointeurs vers un nouveau dossier séparé. Aperçu/application depuis le CLI, l’application native, le Web/API et l’interface JSON C/C++ existante, avec sélection facultative des chemins.
- Vérification des identités d’assets enregistrées et des empreintes des sources avant export. Les exports terminés contiennent un manifeste de provenance ; les interruptions conservent un manifeste incomplet et les copies partielles à examiner.
- Remplacement de l’aide Markdown distribuée par un lecteur CyDoc hors ligne complet : 45 sujets par langue, anglais par défaut et français, aide contextuelle native/Web, liens de pages/sections dans le CLI et archive ZIP complète transférable.
- Génération des références publiques C++/C/extensions depuis les en-têtes distribués, maintien des identifiants d’aide stables et anciens alias pris en charge, et intégration de la génération documentaire à la compilation et aux paquets.
- Prise en charge des variantes de CLI Syncthing supportées et des chemins d’exécutables au démarrage Linux. L’exemple de consommateur C++ gère les chemins Unicode sous Windows.

L’application comprend également les modes Syncthing par projet, sauvegardes versionnées, revue/récupération de fichiers, échanges Git/LFS explicites, extensions natives et fonctions facultatives VPN/tchat/fichiers directs. Le [manuel hors ligne](README.fr.md#manuel-hors-ligne) décrit leur fonctionnement exact et leurs conditions de refus.

## Problèmes connus

Aucune liste distincte de bogues confirmés ne concerne cette version des sources. Consultez le [véritable suivi des problèmes](https://github.com/MrMybal/CySynck/issues) et [ouvrez un signalement](https://github.com/MrMybal/CySynck/issues/new) avec les étapes de reproduction si nécessaire. Les limites ci-dessous ne signifient pas que le logiciel est exempt de bogues.

## Limites documentées

- **Distribution et plateformes** : aucune version binaire de l’application n’est actuellement publiée. Compilation, paquet et exécution Windows x64 ont été validés localement. La CI Linux native valide desktop/serveur, SDK empaqueté et vrais pairs Syncthing. Les contrôles natifs macOS cœur/SDK/paquets passent, dont les captures desktop en rendu logiciel. Matériel graphique, permissions, services de l’hôte et activation des tunnels restent à valider sur leurs cibles.
- **Connectivité VPN** : WireGuard nécessite des endpoints UDP ou IPv6 joignables, une redirection manuelle ou une passerelle PCP/NAT-PMP compatible explicitement activée. STUN/ICE, le perçage NAT arbitraire et les relais de paquets WireGuard ne sont pas implémentés. La découverte et les relais Syncthing peuvent eux-mêmes utiliser une infrastructure ; la traversée CGNAT n’est pas garantie.
- **Messages/fichiers directs** : le canal applicatif authentifié utilise HTTP à l’intérieur du VPN et un secret de groupe partagé. Conservez-le dans le tunnel. La livraison différée hors ligne et les accusés de lecture du tchat ne sont pas fournis.
- **Récupération et accès concurrents** : les opérations multi-fichiers ne sont pas des transactions du système de fichiers. Les verrous applicatifs n’arrêtent pas les éditeurs ou clients Git externes. Gardez des sauvegardes indépendantes ; la restauration générale des ACL, attributs étendus et horodatages d’origine n’est pas garantie.
- **Export d’assets LFS** : seuls les reçus de restauration des pointeurs pris en charge et les fichiers préservés inchangés sont acceptés. Les destinations existantes et les liens sont refusés ; limites de 4 Gio par asset et 16 Gio au total. Un export interrompu nécessite une inspection et une nouvelle destination pour réessayer.
- **Intégration** : les extensions sont du code natif de confiance. Le pont Unreal asynchrone est validé avec Windows Development Editor 5.7.4 et 5.8.3 et un vrai jeu 5.8.3 Win64 Development empaqueté. Autres versions/hôtes Unreal, Shipping et rendu GPU nécessitent leurs propres tests. Close attend le travail actif et peut bloquer. Le transport Git/LFS signé CyRevision est validé contre la référence ; l’admission aux groupes reste gérée par CyRevision.
- **Installation** : Syncthing, Git, les outils Git LFS, WireGuard et OpenSSL s’installent séparément selon les besoins. OpenSSL fournit les signatures P-256 de CyRevision et des adresses VPN. Les installateurs signés, téléchargements automatiques de mises à jour et l’inscription dans les applications installées de Windows ne sont pas implémentés.
- **Manuel local** : l’ouverture réelle dans le navigateur et le stockage en origine fichier restent à vérifier sur les navigateurs/plateformes cibles. Les tests sur modèle DOM et les contrôles des paquets ne prouvent ni le rendu navigateur ni sa politique de stockage.

## Références

- [Dépôt des sources](https://github.com/MrMybal/CySynck) · [Versions](https://github.com/MrMybal/CySynck/releases) · [Problèmes](https://github.com/MrMybal/CySynck/issues).
- [README et compilation](README.fr.md) · [Accès au manuel hors ligne](README.fr.md#manuel-hors-ligne).
- [Licence MIT](LICENSE) · [Notices tierces](THIRD_PARTY.md).
