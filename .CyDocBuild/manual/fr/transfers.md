---
id: transfers
title: Transferts directs avec reprise
description: Envoyer une copie figée par blocs vérifiés et reprendre explicitement une interruption.
section: Réseau
order: 51
parent: network
sourceRevision: 1
---

# Transferts directs avec reprise {#resumable-direct-files-012}

Les fichiers directs utilisent l’identité WireGuard et les autorisations du projet.
Démarrer un récepteur sur la destination (`chat serve` ou vue Réseau) et définir le même secret aléatoire `CYSYNCK_MESH_TOKEN` sur les deux appareils.
HTTP passe à l’intérieur du VPN, sans ajouter TLS, traversée NAT, relais Internet ni livraison différée.

## Envoyer et reprendre {#send-and-resume}

```sh
cysynck send-file PROJECT_ID PEER_ID relative/path.bin
cysynck transfers list PROJECT_ID
cysynck transfers resume PROJECT_ID TRANSFER_ID
cysynck transfers cancel PROJECT_ID TRANSFER_ID --direction out
```

Avant le réseau, l’envoi fige la source dans une copie privée sortante, avec buffer de 64 Kio et SHA-256, sans objet de sauvegarde ni chargement du fichier entier en mémoire.
Une tentative interrompue renvoie `delivered:false`, son `id` et le dernier nombre d’octets acquittés.
La reprise réutilise cette identité/copie même si l’original change ou disparaît.
Relancer `send-file` crée un **nouveau** transfert. La reprise est explicite, sans boucle automatique ; une seule tentative sortante par profil à la fois.
Le récepteur vérifie chaque bloc de 1 Mio, l’écrit atomiquement puis enregistre son hash dans le journal.
À la reprise il vérifie le préfixe stocké avant d’indiquer l’indice suivant.
Un bloc répété identique est accepté ; un bloc changé/hors ordre est refusé.
Le récepteur peut être en avance si un acquittement a été perdu ; la reprise réconcilie cet état.
L’assemblage contrôle taille et SHA-256 complets avant publication du contenu reçu et du reçu durable.
Si l’acquittement a précédé la persistance du reçu, une nouvelle tentative vérifie les octets déjà publiés.
Rejouer une livraison terminée ne crée pas un deuxième message d’historique.
CyUI/Web proposent envoi, actualisation, reprise et retrait local en anglais/français.
La progression est le dernier acquittement sauvé, pas une vitesse réseau instantanée.
Vérifier/exporter les contenus livrés dans [Fichiers reçus](doc:inbox).

## Retrait local et limites {#local-removal-and-limits}

```sh
cysynck transfers cancel PROJECT_ID TRANSFER_ID --direction in
```

Le retrait supprime seulement la copie figée ou les blocs partiels sélectionnés et leur journal local.
Il conserve contenus livrés/reçus et ne contacte pas le pair.
Retirer une réception partielle fait repartir cet ID du bloc zéro lors de la prochaine reprise ; retirer un envoi laisse les blocs du récepteur à retirer là-bas.
Les envois livrés peuvent être retirés pour libérer le catalogue.

- Fichier : **1 Gio** par défaut, réglable de **1 Mio à 4 Gio** ; fichiers vides acceptés.
- Réception : **4 Gio par profil** par défaut, contenus reçus plus taille entière réservée des réceptions partielles, anciens clients compris.
- Copies sortantes en attente : **4 Gio par profil** par défaut.
- Chaque quota se règle de **1 Mio à 64 Gio** et doit couvrir la limite par fichier.
- Au plus **32 réceptions et 32 envois en attente** par profil, fichiers vides compris ; catalogues limités à 10 000 entrées par projet.
- L’assemblage demande temporairement une autre allocation de taille fichier. Les quotas bornent les contenus logiques : prévoir métadonnées, assemblage et copie source figée.

Les limites concernent tous les projets du profil. Les anciens profils sans valeur utilisent les défauts.
Régler les deux appareils avant un envoi plus gros :

```sh
cysynck transfers limits
cysynck transfers configure --file-limit-mib 4096 --inbox-quota-mib 8192 --outbox-quota-mib 8192
```

CyUI/Web exposent les mêmes champs. Valeurs en Mio entiers.
Réduire un quota sous les contenus reçus + réservations, ou sous les copies sortantes en attente, est refusé.
Abaisser la limite fichier affecte les nouveaux transferts ; les partiels/reçus existants restent reprenables, vérifiables et exportables.
L’usage donne `inbox_bytes`, `reserved_incoming_bytes`, `reserved_outgoing_bytes`.
Une capture échouée retire sa copie non publiée ; les dossiers laissés par crash restent pour examen.
Le nettoyage contrôle les noms exacts des blocs en temps linéaire.
Après livraison, blocs partiels et copie sortante sont libérés. Un crash peut laisser `cleanup_pending:true` pour un contenu livré nécessitant un examen local.
Fichiers inattendus, liens, blocs abîmés et dossiers non indexés restent conservés et font refuser le nettoyage au lieu d’une suppression récursive.
Les partiels n’expirent pas automatiquement. Le protocole couvre interruption/reprise, pas la panne de disque.

## Compatibilité et API {#compatibility-and-api}

Choisir explicitement un ancien récepteur :

```sh
cysynck send-file PROJECT_ID PEER_ID relative/path.bin --legacy true
```

Ce mode utilise une requête, 100 Mio au plus et aucune reprise.
Aucun repli automatique ne crée de second transfert. `/file` et `/message` restent acceptés ; anciens reçus/exports restent compatibles.
Un 404 sur une route de reprise est noté dans le journal avec instruction de mise à jour/mode historique.
Les gros transferts version 1 nécessitent un récepteur 0.12+ aux limites adaptées ; les anciens récepteurs à reprise gardent leur plafond de 100 Mio.
Les blocs réseau restent de 1 Mio.
Les échecs ont un `error_code` stable : `interrupted`, `unreachable`, `unsupported_protocol`, `peer_refused`, avec conseils traduits dans les interfaces.
Les routes locales utilisent le jeton API normal :

| Route POST | Corps JSON |
| --- | --- |
| `/api/network/send-file` | `{"project":"ID","peer":"PEER","path":"relative.bin","legacy":false}` |
| `/api/network/transfers` | `{"project":"ID"}` |
| `/api/network/transfer-resume` | `{"project":"ID","transfer":"TRANSFER_ID"}` |
| `/api/network/transfer-cancel` | `{"project":"ID","transfer":"TRANSFER_ID","direction":"in"}` |
| `/api/network/transfer-configure` | `{"file_limit_mib":4096,"inbox_quota_mib":8192,"outbox_quota_mib":8192}` ; champs facultatifs |

GET `/api/network/transfer-limits` renvoie limites effectives, maxima et usage.
SDK C/C++ et CLI distant partagent ces routes. Les réceptions historiques partagent quota/limite, avec plafond de requête 100 Mio.
Le protocole pair version 1 utilise POST `/transfer/begin` (métadonnées JSON), `/transfer/chunk` (binaire brut), `/transfer/finish` (projet/ID JSON).
Tous exigent le Bearer mesh et `X-CySynck-Peer` ; le récepteur remplace le pair JSON par l’identité d’en-tête authentifiée et revérifie adresse VPN/autorisation à chaque requête.
Begin contient `protocol`, `id`, `project`, `name`, `bytes`, `sha256`.
Les blocs portent `X-CySynck-Project`, `X-CySynck-Id`, `X-CySynck-Chunk` à partir de zéro et `X-CySynck-Chunk-SHA256`.
Les acquittements donnent `protocol`, `id`, `next`, `acknowledged_bytes`, `delivered`.
La liste n’expose aucun chemin privé de copie, jeton ou corps d’erreur brut du pair.
