# CySynck — Notes de version

[Retour au README](README.fr.md) · [English](VERSION.md)

**Version des sources du cœur, du CLI et de l’application native : 0.40.0**<br>
**Version des sources du pont Unreal : 0.2.0**<br>
**Statut : EN DÉVELOPPEMENT**<br>
**Binaires de l’application publiés : aucun**

Ces notes décrivent les sources actuelles, pas une version binaire publiée. L’ABI C reste en version 1. La documentation fournie utilise le lecteur CyDoc 0.1.0 ; cette version de dépendance est distincte de celle de CySynck.

## Changements de la version 0.40.0 et des sources actuelles

- Ajout d’un repli OpenGL historique, des diagnostics GLFW et de vérifications réelles du framebuffer des applications empaquetées, dont les contextes macOS sans GPU.
- Pont Unreal 0.2.0 avec requêtes FIFO sur un travailleur, événements Blueprint sur le thread de jeu, file bornée et fermeture/arrêt déterministes. Le script de préparation préserve les extensions existantes ; les tests réels dans Unreal 5.7.4 et 5.8.3 Windows Editor passent.
- Export des assets Git LFS préservés par les reçus de restauration des pointeurs vers un nouveau dossier séparé. Aperçu/application depuis le CLI, l’application native, le Web/API et l’interface JSON C/C++ existante, avec sélection facultative des chemins.
- Vérification des identités d’assets enregistrées et des empreintes des sources avant export. Les exports terminés contiennent un manifeste de provenance ; les interruptions conservent un manifeste incomplet et les copies partielles à examiner.
- Remplacement de l’aide Markdown distribuée par un lecteur CyDoc hors ligne complet : 44 sujets par langue, anglais par défaut et français, aide contextuelle native/Web, liens de pages/sections dans le CLI et archive ZIP complète transférable.
- Génération des références publiques C++/C/extensions depuis les en-têtes distribués, maintien des identifiants d’aide stables et anciens alias pris en charge, et intégration de la génération documentaire à la compilation et aux paquets.
- Prise en charge des variantes de CLI Syncthing supportées et des chemins d’exécutables au démarrage Linux. L’exemple de consommateur C++ gère les chemins Unicode sous Windows.

L’application comprend également les modes Syncthing par projet, sauvegardes versionnées, revue/récupération de fichiers, échanges Git/LFS explicites, extensions natives et fonctions facultatives VPN/tchat/fichiers directs. Le [manuel hors ligne](README.fr.md#manuel-hors-ligne) décrit leur fonctionnement exact et leurs conditions de refus.

## Problèmes connus

Aucune liste distincte de bogues confirmés ne concerne cette version des sources. Consultez le [véritable suivi des problèmes](https://github.com/MrMybal/CySynck/issues) et [ouvrez un signalement](https://github.com/MrMybal/CySynck/issues/new) avec les étapes de reproduction si nécessaire. Les limites ci-dessous ne signifient pas que le logiciel est exempt de bogues.

## Limites documentées

- **Distribution et plateformes** : aucune version binaire de l’application n’est actuellement publiée. Compilation, paquet et exécution Windows x64 ont été validés localement. La CI Linux native valide desktop/serveur, SDK empaqueté et vrais pairs Syncthing. Les tests cœur/SDK macOS passent ; la validation graphique du paquet reste en cours. Matériel graphique, permissions, services de l’hôte et activation des tunnels restent à valider sur leurs cibles.
- **Connectivité VPN** : WireGuard nécessite des points de terminaison UDP joignables ou une configuration réseau adaptée. La traversée NAT automatique, STUN/ICE et les relais de paquets WireGuard ne sont pas implémentés. La découverte et les relais Syncthing peuvent eux-mêmes utiliser une infrastructure.
- **Messages/fichiers directs** : le canal applicatif authentifié utilise HTTP à l’intérieur du VPN et un secret de groupe partagé. Conservez-le dans le tunnel. La livraison différée hors ligne et les accusés de lecture du tchat ne sont pas fournis.
- **Récupération et accès concurrents** : les opérations multi-fichiers ne sont pas des transactions du système de fichiers. Les verrous applicatifs n’arrêtent pas les éditeurs ou clients Git externes. Gardez des sauvegardes indépendantes ; la restauration générale des ACL, attributs étendus et horodatages d’origine n’est pas garantie.
- **Export d’assets LFS** : seuls les reçus de restauration des pointeurs pris en charge et les fichiers préservés inchangés sont acceptés. Les destinations existantes et les liens sont refusés ; limites de 4 Gio par asset et 16 Gio au total. Un export interrompu nécessite une inspection et une nouvelle destination pour réessayer.
- **Intégration** : les extensions sont du code natif de confiance. Le pont Unreal asynchrone est validé avec Windows Development Editor 5.7.4 et 5.8.3 ; autres versions, hôtes Unreal et jeux empaquetés nécessitent leurs propres tests. Close attend le travail actif et peut bloquer. L’interopérabilité du protocole CyRevision n’est pas fournie.
- **Installation** : Syncthing, Git, les outils Git LFS et WireGuard s’installent séparément selon les besoins. Les installateurs signés, téléchargements automatiques de mises à jour et l’inscription dans les applications installées de Windows ne sont pas implémentés.
- **Manuel local** : l’ouverture réelle dans le navigateur et le stockage en origine fichier restent à vérifier sur les navigateurs/plateformes cibles. Les tests sur modèle DOM et les contrôles des paquets ne prouvent ni le rendu navigateur ni sa politique de stockage.

## Références

- [Dépôt des sources](https://github.com/MrMybal/CySynck) · [Versions](https://github.com/MrMybal/CySynck/releases) · [Problèmes](https://github.com/MrMybal/CySynck/issues).
- [README et compilation](README.fr.md) · [Accès au manuel hors ligne](README.fr.md#manuel-hors-ligne).
- [Licence MIT](LICENSE) · [Notices tierces](THIRD_PARTY.md).
