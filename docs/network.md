# Réseau WireGuard et communications directes

Chaque appareil conserve sa clé privée localement et publie un profil public dans
`.cysynck-network/peers/<peer_id>.json` du projet (ou de son dossier d'échange Git).
Syncthing transporte ces profils comme les autres fichiers du dossier partagé.
Les appareils doivent déjà être autorisés dans Syncthing pour cet échange.

Un réseau n'exige pas de serveur VPN central si les appareils peuvent se joindre
en UDP : LAN, IPv6 joignable ou redirection de port. CySynck 0.8 n'implémente pas
encore le hole punching, STUN/ICE ou un relais de paquets WireGuard.
La découverte Syncthing peut elle-même utiliser ses services de découverte et de relais.
Le mode actuel ne promet donc pas une découverte Internet sans aucune infrastructure.

## Préparer deux appareils

Installer WireGuard avec ses outils officiels. `wg` doit être dans le PATH, ou être
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
cysynck vpn trust SHARED_PROJECT_ID OTHER_PEER_ID --key OTHER_PUBLIC_KEY
cysynck vpn preview
cysynck vpn write-config
```

Vérifier la clé publique avec son propriétaire avant de l'autoriser, sur chaque appareil.
Un changement de clé épinglée est refusé ; `vpn revoke PROJECT_ID PEER_ID` retire
l'appairage local. Un peer reçoit seulement une route `/32` vers son adresse VPN.
Aucune route Internet par défaut ni commande PostUp/PostDown n'est générée.

Le fichier privé `vpn/cysynck.conf` reste dans l'état local. Ne jamais le placer dans
un dossier synchronisé. Les permissions POSIX sont limitées au propriétaire ; sous Windows,
utiliser un dossier d'état privé du compte et les protections du système.

## Activer le tunnel

Dans un terminal administrateur/root, **avec le même dossier `--state`** que celui utilisé
par le compte pour préparer la configuration :

```sh
cysynck --state /absolute/path/to/state vpn up
cysynck --state /absolute/path/to/state vpn status
cysynck --state /absolute/path/to/state vpn down
```

Windows utilise le service de tunnel officiel WireGuard ; Linux/macOS utilisent `wg-quick`.
L'interface s'appelle `cysynck`. Ne pas employer ce nom pour un tunnel indépendant.
Changer de configuration après activation nécessite actuellement une reconnexion explicite.
Le bouton desktop tente la même opération avec les droits actuels et signale un échec de droits.

## Tchat et fichiers directs

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
récepteur, utiliser `send-file ... --legacy true`. Voir le [protocole et les limites](transfers.md)
(anglais). Les accusés de lecture et la livraison différée restent à implémenter.
