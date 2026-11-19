---
id: assistant
title: Assistant, automatisation et MCP
description: Utiliser l’assistant natif, automatiser les fonctions métier et connecter un client MCP externe.
section: Développement
order: 72
sourceRevision: 2
---

# Assistant et MCP {#assistant}

CySynck compile CyMCPSDK 0.7.0 depuis ses sources. La révision de référence est
`791d4d7d7680213b282ec8529d248a10f8494d77` ; `cysynck tools info` indique la révision
réellement compilée. Le dépôt voisin `CyMCPSDK` est prioritaire ; définir
`CYSYNCK_CYMCP_SOURCE_DIR` pour choisir un autre dossier. CMake récupère la révision
épinglée du dépôt uniquement lorsqu’aucune source locale n’est disponible.

Modules retenus : Core (schémas, droits, ressources), Platform (processus des
fournisseurs), Http, MCP, AI et ChatUI sur desktop. La supervision et les téléchargements
de modèles du module Local sont désactivés : les agents et services de modèles sont
installés séparément. CyUI fournit Diff, Inspector et DocumentViews avec leurs
dépendances texte/image. CyToolsCore n’était pas une dépendance de l’application.
L’ABI C/C++ et le pont Unreal existants restent indépendants de l’assistant.

## Opérations et autorisations {#operations}

`tools list` donne le catalogue complet. `tools describe NOM` renvoie les schémas
d’entrée/sortie, descriptions et annotations de lecture seule. CLI local, chat natif
et MCP utilisent le même registre et les vrais handlers `Core::request`.
Les actions de projet reçoivent `project_id` ; `parameters` contient leurs arguments
JSON natifs. Les réponses sont enveloppées dans `result`, sauf les exports retenus
qui renvoient `resource`. Les schémas décrivent les paramètres natifs, champs requis
et conditions d’application. Le Core valide aussi les empreintes, chemins et valeurs
des plugins ; les métadonnées de projet/plugin et résultats propres aux modes restent
extensibles. La [référence CLI](doc:cli) décrit les contrats métier natifs.

Le catalogue couvre les projets, sauvegardes, snapshots, restaurations, différences,
conflits, échanges Git et récupération, stockage et options des plugins. Il expose
aussi la configuration et le cycle de vie du moteur, le VPN, les publications signées,
l’appairage/révocation, le chat et les fichiers directs, les quotas/reprises/annulations,
les identités et transactions CyRevision, l’export de services, la maintenance,
le chargement de plugins et les réglages/navigation/aide/démarrage du bureau.
`tools list` reste l’inventaire de référence.

L’accès est en lecture seule par défaut. `--allow-write` en local ou **Autoriser les
actions de l’application** autorise les mutations de cet hôte, y compris les actions
sur les processus et le réseau. Un client distant ne peut pas augmenter ses droits
avec un argument JSON ou CLI. Les plans, empreintes, contrôles de propriété du tunnel,
protections des destinations, quotas et reçus durables natifs restent applicables.
L’envoi de messages/fichiers exige une demande explicite de l’utilisateur. Les clés
et empreintes doivent être confirmées indépendamment par l’utilisateur avant
l’appairage ; leur découverte seule ne constitue pas une confirmation.

| Opérations | Conditions d’exécution |
| --- | --- |
| `cysynck.engine.configure`, `.connect`, `.add-device`, `.share` | Validation native de l’exécutable/API. `.connect` reçoit `api_key_env`, le nom de la variable d’environnement contenant la clé API, jamais le secret en argument/résultat. |
| `cysynck.engine.start`, `.stop` | Hôte persistant : bureau, MCP HTTP ou stdio. Seul le contrôleur propriétaire arrête son processus géré. |
| `cysynck.network.*`, `cysynck.chat.send`, `cysynck.transfers.*` | Privilèges WireGuard, identités épinglées et exigences de VPN/récepteur existantes. Les actions routeur gardent les contrôles natifs de propriété. |
| `cysynck.project.cyrevision-*`, `cysynck.cyrevision.*` | Projet Git exchange, vérification des identités publiques et plans de récupération existants. Les clés privées restent dans le profil local. |
| `cysynck.plugins.load` | Hôte persistant, bibliothèque locale et `expected_sha256` du code vérifié. Le chargement exécute du code natif ; le plugin reste chargé jusqu’à la fermeture de cette instance. |
| `cysynck.receiver.start`, `.stop`, `.status` | Hôte persistant pour le démarrage/arrêt. Le bureau pilote son récepteur existant ; le MCP sans interface possède son propre récepteur. |
| `cysynck.settings.get`, `.set` | Préférences sauvegardées ; `.set` s’applique immédiatement via le point d’accès du bureau, sinon au prochain lancement. |
| `cysynck.ui.status`, `.configure`, `.help`, `cysynck.startup.*` | Point d’accès MCP du bureau ouvert. Champs UI : `ui_view`, `ui_project`, `ui_theme`, `ui_language`, `ui_close_to_tray`, `ui_start_hidden`. Le démarrage utilise l’entrée native appartenant à l’application. |
| `cysynck.service.plan`, `.export`, `cysynck.maintenance.tick` | Export des fichiers de service avec leur plan, sans installation. Tick exécute une fois le planificateur ; un hôte MCP sans interface n’active pas seul une maintenance périodique. |

Les aperçus des opérations aperçu/application restent disponibles en lecture seule ;
`apply:true` exige l’écriture et un plan récent. Les workers SDK utilisent les vrais
handlers Core. Les actions du bureau passent par une file bornée traitée sur le
thread graphique, même lorsqu’une autre vue est affichée. Une action annulée avant
son exécution est retirée ; la fermeture rejette les actions en attente avant de
rejoindre les workers. Une action déjà validée renvoie son reçu malgré l’annulation.
Vérifier l’état durable avant de décider de rejouer une mutation interrompue.

Les jetons API et clés privées VPN/signature ne sont jamais renvoyés. Les projets ne
sont pas indexés comme ressources MCP. Ripgrep et la recherche de fichiers restent
désactivés sans dossier de recherche choisi par l’hôte ; le grep des ressources
examine les textes enregistrés. Le contrôle des scènes/éditeurs Unreal reste un
hôte distinct, connecté par des outils MCP externes explicitement sélectionnés,
sans accès direct aux objets Unreal.

## CLI et instances ouvertes {#cli}

Pour ouvrir le point d’accès du bureau sans passer par ses réglages :
`cysynck-desktop --state /chemin/absolu/profil --mcp-port 0`.
L’URL locale et le chemin du fichier du jeton sont affichés sur stderr. L’écriture
reste désactivée sans **Autoriser les actions de l’application**, ou sans l’option
explicite `--mcp-allow-write`. Utiliser le même profil avec `tools ... --endpoint URL`.
Exemple : `tools call cysynck.ui.configure @ui.json --endpoint URL`, avec
`{"parameters":{"ui_language":"fr","ui_theme":"navy","ui_view":"Overview"}}` dans `ui.json`,
modifie la fenêtre ouverte. `cysynck.ui.status` indique son état réel.

```sh
cysynck --state /chemin/absolu/profil tools help
cysynck --state /chemin/absolu/profil tools info
cysynck --state /chemin/absolu/profil tools list
cysynck --state /chemin/absolu/profil tools describe cysynck.project.create
cysynck --state /chemin/absolu/profil tools call cysynck.overview '{}'
cysynck --state /chemin/absolu/profil tools call cysynck.project.create @create.json --allow-write
```

`create.json` contient `{"parameters":{"name":"Exemple","root":"/projet/existant","mode":"backup"}}`.
Ces commandes utilisent le vrai profil sans ouvrir de fenêtre. stdout reçoit le JSON
et stderr les diagnostics. Code 0 : requête terminée ; 1 : erreur d’exécution/métier ;
2 : syntaxe invalide ; 3 : accès refusé ; 4 : vérification métier `ok:false` (son
rapport JSON reste sur stdout). Un aperçu peut réussir tout en renvoyant des
blocages ou `can_apply:false` : examiner ses champs avant l’application.

Pour cibler l’instance desktop exacte, ouvrir **Assistant**, démarrer son point
d’entrée MCP local et utiliser le port affiché. Son secret Bearer se trouve dans
`api-token` du profil choisi ; le client peut aussi recevoir `CYSYNCK_API_TOKEN` :

```sh
cysynck --state /meme/profil tools list --endpoint http://127.0.0.1:PORT
cysynck --state /meme/profil tools call cysynck.overview '{}' --endpoint http://127.0.0.1:PORT
cysynck --state /meme/profil tools read-resource RESOURCE_URI --endpoint http://127.0.0.1:PORT
```

Ce port est distinct du serveur Web/API `serve`. `--server http://127.0.0.1:8484 ...`
conserve son protocole et ses commandes. Le panneau de commandes historique utilise
les commandes Core ; les lanceurs assistant/MCP appartiennent à l’exécutable console,
également livré dans le paquet.

L’export retenu `snapshot-inventory` exige un hôte actif en mode console : lancer
`mcp --port PORT`, puis appeler l’opération avec `--endpoint`. Ses arguments sont
`{"project_id":"PROJECT_ID","snapshot":"SNAPSHOT_ID"}`. Utiliser `tools read-resource
URI --endpoint URL` pour lire le résultat. Ainsi, aucune référence appartenant à
un processus console déjà terminé n’est renvoyée. Garder l’hôte ouvert pendant
la rétention. Une petite ressource renvoie directement son texte complet ; une
ressource volumineuse fournit un manifeste avec `firstUri`, puis les `nextUri`.
`projects.list` utilise aussi les filtres et paramètres de pagination à la racine,
comme l’indique son schéma.

## Connexion MCP {#mcp}

Un client stdio lance le véritable exécutable console avec ces arguments :

```json
{"command":"/chemin/absolu/bin/cysynck","args":["--state","/chemin/absolu/profil","mcp","--stdio"]}
```

Stdio échange du JSON-RPC par lignes et réserve stdout au protocole. Les droits de
lancement du processus local sont nécessaires ; aucun jeton Bearer supplémentaire
n’est demandé en stdio. Pour HTTP, utiliser **Démarrer le MCP local** ou :

```sh
cysynck --state /chemin/absolu/profil mcp --port 8765
```

Le SDK écoute uniquement sur la boucle locale. Adresse MCP :
`http://127.0.0.1:8765/mcp`, en-tête `Authorization: Bearer JETON`, requêtes JSON et
négociation de session du SDK. Le dispatch natif du SDK est accessible sur `/v1`.
Cette révision prend en charge outils, ressources, pagination bornée, annulation
et abonnements optionnels aux changements. HTTP utilise des réponses JSON ;
l’intégration ne promet pas un flux SSE navigateur reprenable. Les anciens clients
conservent le TTL et la possibilité de vider explicitement leur cache.
`--full` ou **Catalogue MCP complet** expose tous les schémas ; le mode compact
recherche, décrit et appelle les opérations via cinq outils de découverte.

## Chat natif {#chat}

Ouvrir **Assistant** dans la barre latérale. **Détecter les fournisseurs** utilise
un worker annulable du SDK sans envoyer de message ni installer de logiciel.
Choisir une connexion détectée, ou renseigner fournisseur et service/exécutable.
Les secrets API viennent de la variable d’environnement indiquée et restent hors
des historiques/configurations. Choisir un modèle réel du catalogue. La sélection
des modèles d’API distantes reste explicite ; un agent installé ou service local
peut employer sa sélection native/par défaut. La connexion du compte d’un agent
est gérée par son propre système d’authentification local.
La sélection propose Codex et les fabriques OpenAI, Anthropic, OpenRouter, Ollama,
OpenAI-compatible, llama.cpp et LM Studio : elles acceptent réellement le registre
commun dans cette révision. Les autres agents natifs utilisent la connexion MCP
externe ; leur transport de chat n’est pas présenté comme un accès lié aux outils.

ChatUI affiche progressivement le Markdown, les erreurs, l’annulation et les pièces
jointes. Renseigner **Chemin de pièce jointe** puis utiliser l’action du panneau ;
la prise en charge dépend du fournisseur. Aucun fichier n’est joint implicitement.
Les historiques privés optionnels sont dans `assistant-history` du profil. Choisir
un identifiant stable et **Restaurer la session enregistrée** pour la reprendre ;
une session absente/invalide produit une erreur sans substitution automatique.
Le SDK conserve l’historique intégral avec une fenêtre de contexte. Sa limite est
en octets de messages sérialisés, pas une estimation de tokens. Les agents CLI
conservent leur gestion native du contexte. Supprimer soi-même les historiques
devenus inutiles ; leur contenu est une donnée privée locale.
Si le budget ne peut contenir l’échange d’outils actif, la génération signale une
erreur de contexte. Augmenter ou désactiver cette limite ; le SDK ne sépare pas
les appels d’outils de leurs résultats et ne supprime pas silencieusement le tour actif.

## Outils externes {#external}

Les réglages **Outils d’applications externes** reçoivent une URL MCP choisie par
l’utilisateur, une variable de jeton et une liste explicite d’outils autorisés séparés
par des virgules. Découverte et appels utilisent des workers. Les outils deviennent
`remote.HASH_ADRESSE.NOM` ; chaque URL possède son espace de noms. Les outils sélectionnés
exigent l’autorisation locale et les droits du serveur externe. Un résultat distant
`isError` devient une erreur. Des outils list/read permettent de récupérer les
ressources et morceaux distants. Le client suit la pagination de découverte et met
en cache les lectures compatibles de façon privée. Une nouvelle connexion d’un
outil déjà enregistré est refusée ; redémarrer l’hôte assistant pour la remplacer.
Aucun serveur externe n’est activé automatiquement.

Les abonnements du client SDK sont activés lorsque le serveur les accepte, avec
repli sur le TTL. `remote.HASH_ADRESSE.cache.clear` permet de vider explicitement
le cache ; les appels distants le vident aussi et ne sont jamais rejoués. Les
schémas sélectionnés sont un instantané de connexion : redémarrer/reconnecter
l’hôte assistant quand les contrats d’outils distants changent. Les notifications
n’autorisent aucun outil supplémentaire hors de la liste choisie.

## Réglages indépendants {#options}

Le fichier optionnel `assistant-options.json` appartient au profil privé. Redémarrer
l’hôte assistant après modification. Ces réglages sont indépendants et montrent
les valeurs par défaut :

```json
{
  "compact_discovery": true,
  "notifications": true,
  "cache_ttl_ms": 30000,
  "inline_bytes": 16384,
  "disk_spool": false,
  "spool_quota_mib": 128,
  "retention_seconds": 300,
  "metrics": false,
  "context_bytes": 0
}
```

Les gros résultats compacts deviennent des manifestes/morceaux privés. La passerelle
conserve au maximum 16 Mio/128 résultats en mémoire ; le stockage disque optionnel
utilise `assistant-results`, avec quota et expiration. L’opération
`cysynck.project.snapshot-inventory` produit progressivement du JSONL depuis des
pages de 100 fichiers épinglées au manifeste. Lire `firstUri`, puis chaque `nextUri`.
Un lien expiré échoue sans rejouer l’export. Sauvegardes et reçus Git/récupération/
transfert restent dans le stockage durable du Core, indépendamment de cette rétention
temporaire. Les références de résultat ne servent pas de stockage durable de jobs.

Le registre et les ressources partagent le journal de changements du SDK. Les clients
SDK abonnés peuvent invalider la découverte avant le TTL ; les mutations ne sont
jamais mises en cache. Les lectures natives paginées restent accessibles. Les mesures
optionnelles vont dans `assistant-metrics.jsonl`, privé : libellés d’opérations,
durées, volumes, statuts et cache, sans messages, arguments, résultats, secrets ou
identifiants de profils. Aucun résumeur automatique n’est configuré.

## Compilation et validation {#validation}

```sh
cmake -S . -B build -DCYSYNCK_CYMCP_SOURCE_DIR=/sources/CyMCPSDK
cmake --build build --config Release --target cysynck cysynck-desktop cysynck-assistant-tests
ctest --test-dir build -C Release -R '^cysynck-assistant(-cli)?$' --output-on-failure
npm run build --prefix .CyDocBuild
```

`CYSYNCK_DESKTOP=OFF` construit CLI/serveur sans CyUI, ImGui ni ChatUI. Le HTTP du SDK
requiert libcurl de développement sur Linux/macOS. Le paquet contient le code lié
du SDK et ses notices ; fournisseurs et modèles restent externes. Les tests nettoient
leurs propres dossiers isolés. Les fournisseurs simulés vérifient l’intégration,
sans démontrer la compatibilité de tous les comptes/fournisseurs réels. Les validations
de plateforme et de fournisseur réel doivent être rapportées séparément.
