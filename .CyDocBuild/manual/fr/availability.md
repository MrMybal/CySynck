---
id: availability
title: Disponibilité et limites
description: Comprendre les fonctions implémentées et les workflows nécessitant des outils externes.
section: Démarrer
order: 6
parent: overview
sourceRevision: 9
---

# Disponibilité et limites {#availability-and-limits}

## Fonctions implémentées {#implemented-workflows}

CySynck 0.41 comprend les modes par projet, Syncthing supervisé ou connecté, les sauvegardes entières/par blocs fixes/CDC,
la restauration sélective, les différences bornées, la résolution contrôlée des conflits avec conservation des copies, l’échange Git/LFS explicite,
la revue d’historique, l’extraction séparée, les intégrations en avance rapide ou divergentes après revue, le rechargement local du cache LFS,
la matérialisation et sa reprise, la restauration des pointeurs et l’export des assets préservés. Chaque guide donne les limites et conditions de refus exactes.
Les extensions natives utilisent un contrat C versionné. Le cœur C++ et l’ABI C s’utilisent sans le desktop.
L’[adaptateur de transport CyRevision](doc:cyrevision) ajoute les publications Git/LFS signées
et les appareils de publication approuvés par empreinte, avec de vrais tests dans les deux sens.
Les adhésions et rôles restent gérés par CyRevision ; l’autorisation du transport ne vaut pas admission au groupe.

## Conditions réseau {#network-requirements}

WireGuard nécessite des endpoints UDP joignables ou une configuration réseau adaptée. STUN/ICE, le perçage NAT automatique et le relais de paquets WireGuard ne sont pas implémentés.
La découverte d’adresses signées à durée limitée, les compteurs privés contre le rejeu, les indices LAN/IPv6 directs de Syncthing et l’actualisation automatique optionnelle sont implémentés. Les mises à jour actives et révocations par projet passent les modèles isolés de propriété et d’outils. Un vrai test Linux en espaces réseau passe aussi l’échange de clés, le tchat, les fichiers vérifiés, la révocation/redémarrage et la préservation d’une interface étrangère, avec restauration des règles AppArmor limitées ensuite. Les pilotes Windows/macOS restent à vérifier. Un second vrai test Linux passe deux routeurs NAT miniupnpd indépendants, les baux PCP/NAT-PMP, le renouvellement planifié, la découverte signée, le tchat/fichiers vérifiés et la préservation d’un autre port redirigé. Le consentement explicite PCP/NAT-PMP, les baux privés persistants, leur renouvellement/expiration, la liaison à l’adresse source, le nettoyage limité au bail possédé et la publication signée des adresses passent les tests de protocole, contrôleur et Core. Voir la [configuration réseau](doc:network#automatic-endpoints) et la [redirection du routeur](doc:network#router-mapping).
La découverte et les relais Syncthing peuvent utiliser une infrastructure. Le tchat et les fichiers directs passent par HTTP authentifié dans le VPN, avec un secret de groupe partagé.
Il n’existe pas de livraison différée hors ligne ni d’accusé de lecture. Ne pas exposer ce canal applicatif non chiffré hors du tunnel.

## Plateformes et intégrations {#platform-and-integration-scope}

Windows est la plateforme de validation d’exécution locale. La CI Linux native passe aussi les tests desktop/serveur, paquets/SDK et vrais pairs Syncthing.
Les contrôles natifs macOS cœur/SDK/paquets passent, dont les captures OpenGL 3 et OpenGL 2 en rendu logiciel. CI et compilation croisée ne valident pas le matériel graphique, l’activation des services de l’hôte, les permissions des tunnels ou le comportement en cas de coupure.
Le pont Unreal fournit les requêtes Blueprint FIFO asynchrones. Compilation/tests Windows Development Editor 5.7.4 et 5.8.3 réussis. Un vrai jeu Win64 Development empaqueté sous 5.8.3 passe cinq tests, avec 34 fichiers runtime/licences/manuel vérifiés. NullRHI ne prouve pas le rendu ; autres versions/hôtes Unreal et configurations Shipping nécessitent leurs propres validations. Close attend l’opération active et peut bloquer.
Git, les outils de suivi Git LFS, Syncthing, WireGuard et OpenSSL s’installent séparément selon les besoins.

## Conserver des copies de récupération indépendantes {#preserve-independent-recovery-copies}

Les sauvegardes/restaurations multi-fichiers, mises à jour Git, écritures de reçus et publications de récupération ne forment pas une transaction unique du système de fichiers.
Les verrous de CySynck coordonnent ses propres processus ; les éditeurs et clients Git externes peuvent toujours écrire. Suspendre ces écritures pendant les opérations avec revue.
Les permissions simples sont reprises là où c’est documenté ; la restauration générale des ACL, attributs étendus et dates originales n’est pas garantie.
Les checksums vérifient l’intégrité, pas l’authenticité face à un acteur pouvant changer les données et leurs métadonnées.
Conserver des sauvegardes indépendantes avant de modifier un dépôt important ou d’appliquer une récupération.
