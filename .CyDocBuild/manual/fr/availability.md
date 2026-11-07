---
id: availability
title: Disponibilité et limites
description: Comprendre les fonctions implémentées et les workflows nécessitant des outils externes.
section: Démarrer
order: 6
parent: overview
sourceRevision: 1
---

# Disponibilité et limites {#availability-and-limits}

## Fonctions implémentées {#implemented-workflows}

CySynck 0.40 comprend les modes par projet, Syncthing supervisé ou connecté, les sauvegardes entières/par blocs fixes/CDC,
la restauration sélective, les différences bornées, la résolution contrôlée des conflits avec conservation des copies, l’échange Git/LFS explicite,
la revue d’historique, l’extraction séparée, les intégrations en avance rapide ou divergentes après revue, le rechargement local du cache LFS,
la matérialisation et sa reprise, la restauration des pointeurs et l’export des assets préservés. Chaque guide donne les limites et conditions de refus exactes.
Les extensions natives utilisent un contrat C versionné. Le cœur C++ et l’ABI C s’utilisent sans le desktop.

## Conditions réseau {#network-requirements}

WireGuard nécessite des endpoints UDP joignables ou une configuration réseau adaptée. STUN/ICE, le perçage NAT automatique et le relais de paquets WireGuard ne sont pas implémentés.
La découverte et les relais Syncthing peuvent utiliser une infrastructure. Le tchat et les fichiers directs passent par HTTP authentifié dans le VPN, avec un secret de groupe partagé.
Il n’existe pas de livraison différée hors ligne ni d’accusé de lecture. Ne pas exposer ce canal applicatif non chiffré hors du tunnel.

## Plateformes et intégrations {#platform-and-integration-scope}

Windows est la plateforme disponible pour les validations d’exécution locales. Les builds natifs Linux/macOS et leurs tests relèvent de la CI et de la validation sur les machines cibles.
La compilation croisée d’un serveur Linux ne valide pas son desktop, ses permissions, l’activation du tunnel ou son comportement en cas de coupure.
Le pont Unreal fourni est synchrone et doit être compilé et validé avec la version d’Unreal choisie ; aucun ordonnanceur automatique de Blueprints asynchrones n’est fourni.
Git, les outils de suivi Git LFS, Syncthing et WireGuard s’installent séparément selon les besoins.

## Conserver des copies de récupération indépendantes {#preserve-independent-recovery-copies}

Les sauvegardes/restaurations multi-fichiers, mises à jour Git, écritures de reçus et publications de récupération ne forment pas une transaction unique du système de fichiers.
Les verrous de CySynck coordonnent ses propres processus ; les éditeurs et clients Git externes peuvent toujours écrire. Suspendre ces écritures pendant les opérations avec revue.
Les permissions simples sont reprises là où c’est documenté ; la restauration générale des ACL, attributs étendus et dates originales n’est pas garantie.
Les checksums vérifient l’intégrité, pas l’authenticité face à un acteur pouvant changer les données et leurs métadonnées.
Conserver des sauvegardes indépendantes avant de modifier un dépôt important ou d’appliquer une récupération.
