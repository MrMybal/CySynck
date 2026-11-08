---
id: network
title: Réseau WireGuard et communications directes
description: Utilisation, commandes et limites : Réseau WireGuard et communications directes.
section: Réseau
order: 50
sourceRevision: 3
---

# Réseau WireGuard et communications directes {#reseau-wireguard-et-communications-directes}

Chaque appareil conserve sa clé privée localement et publie un profil public dans
`.cysynck-network/peers/<peer_id>.json` du projet (ou de son dossier d'échange Git).
Syncthing transporte ces profils comme les autres fichiers du dossier partagé.
Les appareils doivent déjà être autorisés dans Syncthing pour cet échange.

Un réseau n'exige pas de serveur VPN central si les appareils peuvent se joindre
en UDP : LAN, IPv6 joignable ou redirection de port. CySynck n'implémente pas
encore le hole punching, STUN/ICE ou un relais de paquets WireGuard.
La découverte Syncthing peut elle-même utiliser ses services de découverte et de relais.
Le mode actuel ne promet donc pas une découverte Internet sans aucune infrastructure.

## Préparer deux appareils {#preparer-deux-appareils}

Installer les outils officiels WireGuard et OpenSSL (signatures P-256 des adresses).
`CYSYNCK_OPENSSL` peut sélectionner l’exécutable OpenSSL. `wg` doit être dans le PATH, ou être
sélectionné par `CYSYNCK_WG`. Sous Windows, `CYSYNCK_WIREGUARD` peut préciser le chemin de
`wireguard.exe`. Sous Linux/macOS, `CYSYNCK_WG_QUICK` peut préciser `wg-quick`.
Attribuer une adresse **différente** de `10.77.0.0/16` à chaque appareil, en évitant un
conflit avec les réseaux déjà utilisés. Ce sous-réseau n'est pas encore paramétrable.

```sh
# Appareil A
cysynck vpn configure Alice 10.77.0.1 --endpoint alice.example.net:51820
cysynck vpn publish SHARED_PROJECT_ID

# Appareil B
cysynck vpn configure Bob 10.77.0.2 --endpoint bob.example.net:51820
cysynck vpn publish SHARED_PROJECT_ID
```

Les endpoints sont des adresses UDP WireGuard ; ne pas y copier le port TCP/QUIC
Syncthing ou l'adresse de son relais. Le port local par défaut est `51820/udp`.
Un appareil derrière NAT peut laisser son endpoint vide si l'autre est joignable ;
il doit initier des paquets vers l'appareil joignable. Un `PersistentKeepalive` de
25 secondes est ajouté aux pairs avec un endpoint connu.

Après synchronisation des profils :

```sh
cysynck vpn discover SHARED_PROJECT_ID
cysynck vpn trust SHARED_PROJECT_ID OTHER_PEER_ID --key OTHER_PUBLIC_KEY --fingerprint VERIFIED_SIGNER_SHA256
cysynck vpn preview
cysynck vpn write-config
```

Vérifier la clé VPN et l’empreinte de signature des adresses avec leur propriétaire via un canal de confiance séparé, sur chaque appareil. `vpn status` affiche l’empreinte locale ; `vpn discover` affiche celle des candidats. Le formulaire natif demande les valeurs vérifiées indépendamment. Changer une clé, une adresse, un appareil Syncthing, un port d’écoute ou une identité de signature épinglés nécessite une révocation puis un nouvel appairage. Les anciens profils sans clé de signature restent utilisables pour un appairage statique.

`vpn revoke PROJECT_ID PEER_ID` retire l’autorisation de ce projet. Un pair encore autorisé dans un autre projet reste dans le tunnel. Retirer sa dernière autorisation supprime aussi le pair WireGuard actif et actualise une configuration privée existante pour empêcher sa restauration au redémarrage du service. Un échec d’outil ou de permissions conserve une intention privée de suppression et retourne `requires_restart: true` ; l’autorisation applicative est déjà retirée. Réessayer avec `vpn refresh` et les droits nécessaires, ou arrêter puis redémarrer le tunnel. Un pair reçoit seulement une route `/32` vers son adresse VPN.
Aucune route Internet par défaut ni commande PostUp/PostDown n'est générée.

Le fichier privé `vpn/cs-EMPREINTE_PROFIL.conf` reste dans l’état local. Son nom de tunnel
stable de 15 caractères est dérivé de l’identité du pair et affiché par `vpn preview`,
`vpn write-config` et `vpn status`. Chaque profil dispose de son propre nom. Ne jamais
le placer dans un dossier synchronisé. Les permissions POSIX sont limitées au propriétaire ; sous Windows,
utiliser un dossier d'état privé du compte et les protections du système.

## Activer le tunnel {#activer-le-tunnel}

CySynck vérifie la clé publique de l’interface existante avant de la contrôler. Une clé
étrangère ou un service Windows associé à une autre configuration provoque un refus.
Le service Windows doit utiliser l’exécutable, la commande et le chemin privé exacts
attendus. Un échec d’inspection ne vaut pas absence de tunnel. Sous macOS, le nom logique
est relié à l’interface `utun` via le mapping de wg-quick, puis la clé est vérifiée.
Ces contrôles ne remplacent pas les permissions administrateur/root du système.

Arrêter un ancien tunnel global `cysynck` appartenant au profil avec `vpn down` avant
de démarrer le nouveau tunnel propre au profil. Son ancienne configuration doit appartenir
au profil ; les tunnels globaux étrangers sont préservés. L’arrêt régénère une configuration
minimale sans hooks au lieu d’exécuter les commandes d’un ancien fichier modifié.
Répéter up/down ne remplace pas un tunnel actif et ne réactive pas un tunnel absent.
Si un service Windows existe sans interface active, `already_up` est faux ; l’arrêter avant
de réessayer. Un arrêt/démarrage applique les changements d’adresse ou de port d’écoute local. `vpn refresh` peut appliquer les adresses des pairs et les révocations au tunnel actif appartenant au profil.

Dans un terminal administrateur/root, **avec le même dossier `--state`** que celui utilisé
par le compte pour préparer la configuration :

```sh
cysynck --state /absolute/path/to/state vpn up
cysynck --state /absolute/path/to/state vpn status
cysynck --state /absolute/path/to/state vpn down
```

Windows utilise le service de tunnel officiel WireGuard ; Linux/macOS utilisent `wg-quick`.
Utiliser le nom d’interface propre au profil affiché par `vpn status`.
Le bureau tente les mêmes opérations avec ses droits actuels et signale les échecs de permissions.

## Actualisation automatique des adresses {#automatic-endpoints}

Laisser vide l’endpoint publié d’un pair pour autoriser la sélection automatique. Un endpoint explicitement épinglé reste prioritaire. `vpn refresh [PROJECT_ID]` lit les publications signées `<peer_id>.endpoint.json` uniquement pour les projets et identités déjà autorisés. Chaque publication lie le projet, le pair, sa clé WireGuard, sa clé de signature, son appareil Syncthing et son port UDP. Les signatures expirent au plus tard après une heure ; les compteurs et empreintes privés refusent les retours en arrière et réutilisations divergentes après redémarrage. Une publication vérifiée en cache reste utilisable jusqu’à son expiration si un ancien fichier ou un faux remplace le fichier partagé. Révoquer l’autorisation d’un projet retire son adresse des candidates utilisables.

Sans publication signée valide, la connexion Syncthing locale peut fournir une adresse IPv4 privée du LAN ou IPv6 globale/ULA directe. Seuls les pairs TCP/QUIC connectés avec un identifiant d’appareil Syncthing épinglé sont retenus. Le port UDP provient du **port d’écoute WireGuard** épinglé, jamais du port observé de synchronisation. Les relais, appareils déconnectés, indices IPv4 publics derrière NAT, adresses de boucle locale, link-local et noms d’hôte sont exclus. Une candidate ne prouve pas la connectivité UDP : vérifier un échange de clés WireGuard récent dans `vpn status`.

```sh
cysynck vpn refresh SHARED_PROJECT_ID
cysynck vpn automatic true
# Garder le bureau ou `cysynck serve` actif pour appeler Core::tick().
cysynck vpn automatic false
```

L’actualisation automatique est désactivée par défaut et vérifie environ toutes les 45 secondes pendant l’exécution de l’hôte. Elle ne crée jamais de tunnel ; modifier un tunnel actif nécessite les droits administrateur/root et les contrôles de propriété. Une adresse dynamique expirée disparaît de la configuration générée et est effacée du pair actif avant de rétablir sa route hôte. Les erreurs sont disponibles dans `endpoint_refresh` de `vpn status`. HTTP et le CLI distant ne peuvent pas configurer les identités, appairer/révoquer des pairs, écrire la configuration privée, activer le tunnel ou activer/exécuter l’actualisation automatique ; utiliser le bureau ou CLI local.

Les nouvelles identités conservent leur clé de signature séparée dans `vpn/endpoint-signing.pem`, jamais dans le dossier d’échange. Publier un endpoint manuel écrit aussi une publication signée valable 15 minutes. Les clients de protocole PCP/NAT-PMP sont testés séparément, mais leur configuration, le renouvellement des baux et la publication des adresses mappées sont encore en cours d’intégration. Cette version n’ouvre pas encore automatiquement de redirection sur le routeur.

## Tchat et fichiers directs {#tchat-et-fichiers-directs}

Définir `CYSYNCK_MESH_TOKEN` à un même secret aléatoire d'au moins 24 caractères sur les
appareils autorisés. Le partager hors du dossier de profils publics. Cette version emploie
un secret de groupe, pas encore un secret distinct par paire.

```sh
cysynck chat serve
cysynck chat send PROJECT_ID OTHER_PEER_ID "Bonjour"
cysynck send-file PROJECT_ID OTHER_PEER_ID assets/example.bin
cysynck chat history PROJECT_ID
```

Le récepteur écoute sur l'adresse VPN locale, par défaut au port `8485/tcp`.
Le desktop peut démarrer ce même récepteur depuis la vue Réseau. Un seul récepteur doit
écouter sur cette adresse/ce port ; choisir entre le CLI et le desktop.
Le nom de fichier envoyé est un chemin relatif au projet. Les octets reçus sont conservés
sous un identifiant généré dans `inbox/<project>/<id>.received`, avec leur nom original
dans l'historique. Ils ne sont pas ouverts, exécutés ou insérés dans le projet automatiquement.

Le canal applicatif est HTTP **à l'intérieur du tunnel WireGuard** ; il ne fournit pas
son propre chiffrement TLS. Les connexions sortantes se lient à l'adresse VPN configurée.
Le récepteur vérifie l'adresse VPN, le peer épinglé, son autorisation pour le projet et
le secret de groupe. Ne pas configurer ces adresses sur une interface publique ou en clair.

Limites par défaut : 16 Ko par message UTF-8, 1 Gio par fichier, 4 Gio par quota de réception et d'envoi,
2000 éléments d'historique local. Depuis la version 0.10, les fichiers sont envoyés par blocs
de 1 Mio vérifiés, avec copie source figée et reprise explicite après interruption :
`transfers list PROJECT_ID`, puis `transfers resume PROJECT_ID TRANSFER_ID`.
Depuis la version 0.12, `transfers configure` et les interfaces permettent de régler les limites
jusqu'à 4 Gio par fichier et 64 Gio par quota. Ces valeurs concernent tous les projets du profil.
Les réservations des fichiers en cours comptent dans le quota de réception. Pour un ancien
récepteur, utiliser `send-file ... --legacy true`. Voir le [protocole et les limites](doc:transfers)
. Les accusés de lecture et la livraison différée restent à implémenter.
