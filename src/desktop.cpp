#include <cysynck/core.hpp>
#include <cyui/Core.h>
#include <cyui/modules/Diff.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
#include <GLFW/glfw3.h>
#include <fstream>
#include <functional>
#include <future>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <misc/cpp/imgui_stdlib.h>
#include <thread>
#include <set>
#include <unordered_map>
#include <utility>
#include "desktop_support.hpp"
#include "startup.hpp"
using cysynck::Json;
namespace fs = std::filesystem;
namespace c = cyui;
static std::vector<std::string> split(const std::string &input) {
    std::vector<std::string> args;
    std::string token;
    char quote = 0;
    bool active = false;
    for (char ch : input) {
        if (ch == '"' || ch == '\'') {
            if (quote == ch)
                quote = 0;
            else if (!quote)
                quote = ch;
            else
                token += ch;
            active = true;
        } else if (ch == ' ' && !quote) {
            if (active) {
                args.push_back(token);
                token.clear();
                active = false;
            }
        } else {
            token += ch;
            active = true;
        }
    }
    if (quote)
        throw std::runtime_error("Unclosed quote");
    if (active)
        args.push_back(token);
    if (!args.empty() && args[0] == "cysynck")
        args.erase(args.begin());
    return args;
}
static bool button(const char *name, bool primary = false) {
    auto label = c::trLabel(name);
    return primary ? c::primaryButton(label.c_str()) : c::button(label.c_str());
}
static void text(const std::string &s) { ImGui::TextUnformatted(s.c_str()); }
static void field(const char *label, std::string &value) {
    c::property(c::tr(label).c_str());
    ImGui::InputText((std::string("##") + label).c_str(), &value);
}
static void localize() {
    c::registerTranslations(
        "fr",
        {{"Direct transfers", "Transferts directs"},
         {"Git exchange storage", "Stockage des échanges Git"},
         {"Preview Git cleanup", "Prévisualiser le nettoyage Git"},
         {"Apply Git cleanup", "Appliquer le nettoyage Git"},
         {"Select exports to remove. Peers can no longer import removed exports. Local Git data is "
          "preserved.",
          "Sélectionnez les exports à supprimer. Les pairs ne pourront plus les importer. Les "
          "données Git locales sont préservées."},
         {"Unused LFS objects", "Objets LFS inutilisés"},
         {"Reclaimable bytes", "Octets récupérables"},
         {"Imported branches", "Branches importées"},
         {"Review branch", "Revoir la branche"},
         {"Review file", "Revoir le fichier"},
         {"Local commits", "Commits locaux"},
         {"Peer commits", "Commits du pair"},
         {"Branches are identical", "Les branches sont identiques"},
         {"Fast-forward is possible", "Avance rapide possible"},
         {"Local branch is ahead", "La branche locale est en avance"},
         {"Branches have diverged", "Les branches ont divergé"},
         {"Histories are unrelated", "Les historiques sont indépendants"},
         {"LFS pointer comparison; asset bytes are not loaded.",
          "Comparaison des pointeurs LFS ; les octets des assets ne sont pas chargés."},
         {"Git text previews are limited to 512 KiB per side",
          "Les aperçus de texte Git sont limités à 512 Kio par côté"},
         {"Binary or non-UTF-8 Git object; text preview unavailable",
          "Objet Git binaire ou non UTF-8 ; aperçu textuel indisponible"},
         {"Submodule commit changed; inspect its repository separately",
          "Le commit du sous-module a changé ; examinez son dépôt séparément"},
         {"Use Git LFS during synchronization", "Utiliser Git LFS pendant la synchronisation"},
         {"Approved LFS storage directory", "Dossier de stockage LFS autorisé"},
         {"Leave empty for the default Git cache. A custom directory must already exist and match "
          "Git lfs.storage.",
          "Laissez vide pour le cache Git par défaut. Un dossier personnalisé doit déjà exister et "
          "correspondre à Git lfs.storage."},
         {"Include Git LFS history", "Inclure l'historique Git LFS"},
         {"Git LFS objects", "Objets Git LFS"},
         {"Waiting for Git LFS objects", "En attente des objets Git LFS"},
         {"Git and LFS imports preserve the working tree. Review and merge using Git.",
          "Les imports Git et LFS préservent le dossier de travail. Vérifiez et fusionnez avec "
          "Git."},
         {"Profile transfer limits", "Limites de transfert du profil"},
         {"These limits apply to all projects. Existing transfers keep their reserved size.",
          "Ces limites concernent tous les projets. Les transferts existants conservent leur "
          "taille réservée."},
         {"File limit (MiB)", "Limite par fichier (Mio)"},
         {"Inbox quota (MiB)", "Quota de réception (Mio)"},
         {"Outbox quota (MiB)", "Quota d'envoi (Mio)"},
         {"Save transfer limits", "Enregistrer les limites de transfert"},
         {"Inbox bytes", "Octets reçus"},
         {"Reserved incoming bytes", "Octets réservés en réception"},
         {"Reserved outgoing bytes", "Octets réservés en envoi"},
         {"Reuse blocks for large files", "Réutiliser les blocs des gros fichiers"},
         {"Blocks of 4 MiB are reused for files from 8 MiB to 256 GiB.",
          "Les blocs de 4 Mio sont réutilisés pour les fichiers de 8 Mio à 256 Gio."},
         {"Block data written", "Données de blocs écrites"},
         {"Block data reused", "Données de blocs réutilisées"},
         {"Deduplicated data", "Données dédupliquées"},
         {"Block objects", "Objets de blocs"},
         {"Chunked file versions", "Versions de fichiers par blocs"},
         {"This peer needs an update or legacy mode.",
          "Ce pair nécessite une mise à jour ou le mode ancien pair."},
         {"Refresh transfers", "Actualiser les transferts"},
         {"No direct transfers", "Aucun transfert direct"},
         {"Outgoing transfer", "Transfert sortant"},
         {"Incoming transfer", "Transfert entrant"},
         {"Delivered", "Livré"},
         {"Waiting for continuation", "En attente de continuation"},
         {"Resume transfer", "Reprendre le transfert"},
         {"Remove local transfer", "Retirer le transfert local"},
         {"Legacy peer (single request)", "Ancien pair (requête unique)"},
         {"Resume uses a frozen copy of the source. Both peers need an active VPN and receiver.",
          "La reprise utilise une copie figée de la source. Les deux pairs doivent avoir un VPN et "
          "un récepteur actifs."},
         {"Transfer interrupted. Check receiver, pairing and VPN, then resume.",
          "Transfert interrompu. Vérifiez le récepteur, l'appairage et le VPN, puis reprenez."},
         {"Removal clears local partial data or transfer history. Received files are kept; the "
          "other peer is unchanged.",
          "Le retrait efface les données partielles locales ou l'historique du transfert. Les "
          "fichiers reçus sont conservés et l'autre pair reste inchangé."},
         {"Services", "Services"},
         {"Target platform", "Plateforme cible"},
         {"CLI on target", "CLI sur la cible"},
         {"State on target", "Profil sur la cible"},
         {"API port", "Port API"},
         {"Prepare service", "Préparer le service"},
         {"Export service files", "Exporter les fichiers du service"},
         {"Service files exported", "Fichiers du service exportés"},
         {"Review the target paths and port. Export does not install or start a service.",
          "Vérifiez les chemins et le port sur la cible. L'export n'installe ni ne démarre de "
          "service."},
         {"Open the exported README on the target machine for installation and removal commands.",
          "Ouvrez le README exporté sur la machine cible pour les commandes d'installation et de "
          "suppression."},
         {"Diagnostics", "Diagnostic"},
         {"Inspect diagnostics", "Actualiser le diagnostic"},
         {"Test tools and connection", "Tester les outils et la connexion"},
         {"Errors", "Erreurs"},
         {"Warnings", "Avertissements"},
         {"Tool not found", "Outil introuvable"},
         {"Tool available", "Outil disponible"},
         {"Version probe failed", "Échec du test de version"},
         {"WireGuard tools", "Outils WireGuard"},
         {"WireGuard activation", "Activation WireGuard"},
         {"Syncthing connection", "Connexion Syncthing"},
         {"Syncthing API", "API Syncthing"},
         {"Connection configured", "Connexion configurée"},
         {"Connection not configured", "Connexion non configurée"},
         {"Connection available", "Connexion disponible"},
         {"Connection unavailable", "Connexion indisponible"},
         {"Directory available", "Dossier disponible"},
         {"Directory missing or linked", "Dossier absent ou lié"},
         {"Git mode needs attention", "Mode Git à vérifier"},
         {"Recovery review required", "Vérification de récupération nécessaire"},
         {"Install Syncthing or configure an existing instance.",
          "Installez Syncthing ou configurez une instance existante."},
         {"Install Git and add it to PATH.", "Installez Git et ajoutez-le au PATH."},
         {"Install WireGuard tools or configure CYSYNCK_WG.",
          "Installez les outils WireGuard ou configurez CYSYNCK_WG."},
         {"Install the official WireGuard application.",
          "Installez l'application officielle WireGuard."},
         {"Install wg-quick or configure CYSYNCK_WG_QUICK.",
          "Installez wg-quick ou configurez CYSYNCK_WG_QUICK."},
         {"Configure Syncthing in the Engine view.", "Configurez Syncthing dans la vue Moteur."},
         {"Start Syncthing and verify the configured connection.",
          "Démarrez Syncthing et vérifiez la connexion configurée."},
         {"Restore the project directory or review its path.",
          "Rétablissez le dossier du projet ou vérifiez son chemin."},
         {"Use Git exchange for repositories; keep each active .git local.",
          "Utilisez l'échange Git pour les dépôts ; gardez chaque .git actif localement."},
         {"Review interrupted resolutions before resuming this project.",
          "Vérifiez les résolutions interrompues avant de reprendre ce projet."},
         {"Overview", "Vue d'ensemble"},
         {"Projects", "Projets"},
         {"Received files", "Fichiers reçus"},
         {"Open received files", "Ouvrir les fichiers reçus"},
         {"Back to network", "Retour au réseau"},
         {"No received files", "Aucun fichier reçu"},
         {"Verify received file", "Vérifier le fichier reçu"},
         {"Export received file", "Exporter le fichier reçu"},
         {"Received file verified", "Fichier reçu vérifié"},
         {"Received file exported", "Fichier reçu exporté"},
         {"Last verification succeeded", "Dernière vérification réussie"},
         {"Payload unavailable", "Fichier reçu indisponible"},
         {"Unindexed inbox files are preserved and require manual review.",
          "Les fichiers reçus sans référence sont conservés et nécessitent un examen manuel."},
         {"Export creates a new separate directory and preserves the received copy.",
          "L'export crée un nouveau dossier séparé et conserve la copie reçue."},
         {"New project", "Nouveau projet"},
         {"Changes", "Différences"},
         {"Conflicts", "Conflits"},
         {"Resolve conflict", "Résoudre le conflit"},
         {"Keep current version", "Garder la version courante"},
         {"Keep deletion", "Conserver la suppression"},
         {"Use conflict copy", "Utiliser la copie de conflit"},
         {"Manual merge", "Fusion manuelle"},
         {"Choice", "Choix"},
         {"Prepare resolution", "Préparer la résolution"},
         {"Apply resolution", "Appliquer la résolution"},
         {"Resolution applied", "Résolution appliquée"},
         {"Not enough free space for recovery copies.",
          "Espace libre insuffisant pour les copies de récupération."},
         {"Recovery", "Récupération"},
         {"Recovery history", "Historique des résolutions"},
         {"Back to conflicts", "Retour aux conflits"},
         {"No resolution history", "Aucune résolution enregistrée"},
         {"Export recovery copies", "Exporter les copies de récupération"},
         {"Recovery directory", "Dossier de récupération"},
         {"Needs recovery", "Récupération à examiner"},
         {"Applied", "Appliqué"},
         {"Rolled back", "Annulé"},
         {"Reviewed", "Examiné"},
         {"Mark recovery as reviewed", "Marquer la récupération comme examinée"},
         {"The project must be paused before applying a choice. Resume synchronization explicitly "
          "after checking the result.",
          "Le projet doit être en pause pour appliquer un choix. Reprenez explicitement la "
          "synchronisation après avoir vérifié le résultat."},
         {"Both source versions are preserved. The prepared choice changes the local file.",
          "Les deux versions sources sont conservées. Le choix préparé modifie le fichier local."},
         {"Manual editor uses LF line endings. Maximum 512 KiB.",
          "L'éditeur manuel utilise des fins de ligne LF. Maximum : 512 Kio."},
         {"Recovery copies are exported to a new separate directory.",
          "Les copies de récupération sont exportées dans un nouveau dossier séparé."},
         {"After restoring or checking the working files, mark this recovery as reviewed. The "
          "backups are kept.",
          "Après avoir restauré ou vérifié les fichiers de travail, marquez cette récupération "
          "comme examinée. Les sauvegardes sont conservées."},
         {"Find conflicts", "Rechercher les conflits"},
         {"No conflict copies found", "Aucune copie de conflit trouvée"},
         {"Select a conflict copy to compare its contents with the current file.",
          "Sélectionnez une copie de conflit pour la comparer au fichier courant."},
         {"Current file", "Fichier courant"},
         {"Conflict copy", "Copie de conflit"},
         {"Original file is missing", "Le fichier original est absent"},
         {"Source", "Source"},
         {"Snapshot", "Sauvegarde"},
         {"Two project files", "Deux fichiers du projet"},
         {"Left relative path", "Chemin relatif à gauche"},
         {"Right relative path", "Chemin relatif à droite"},
         {"Copy patch", "Copier le patch"},
         {"Patch copied", "Patch copié"},
         {"No differences", "Aucune différence"},
         {"Comparison details", "Détails de la comparaison"},
         {"Left side", "Côté gauche"},
         {"Right side", "Côté droit"},
         {"Diagnostics", "Diagnostic"},
         {"Only the first 10000 conflict copies are listed.",
          "Seules les 10 000 premières copies de conflit sont affichées."},
         {"The patch replaces a large changed block; it is not a minimal diff.",
          "Le patch remplace un grand bloc modifié ; le diff n'est pas minimal."},
         {"Binary or non-UTF-8 file", "Fichier binaire ou non UTF-8"},
         {"Text previews are limited to 512 KiB per side", "Aperçu limité à 512 Kio par côté"},
         {"Text previews are limited to 10000 lines", "Aperçu limité à 10 000 lignes"},
         {"Backups", "Sauvegardes"},
         {"Storage", "Stockage"},
         {"All projects", "Tous les projets"},
         {"Inspect storage", "Analyser le stockage"},
         {"Stored", "Stocké"},
         {"Reclaimable", "Récupérable"},
         {"Unused objects", "Objets inutilisés"},
         {"Free unused space", "Libérer l'espace inutilisé"},
         {"Storage cleanup complete", "Nettoyage du stockage terminé"},
         {"Cleanup blocked", "Nettoyage bloqué"},
         {"Missing objects", "Objets manquants"},
         {"Verify backup", "Vérifier la sauvegarde"},
         {"Integrity verified", "Intégrité vérifiée"},
         {"Integrity check failed", "Échec du contrôle d'intégrité"},
         {"Preview restoration", "Préparer la restauration"},
         {"Files", "Fichiers"},
         {"Snapshot", "Sauvegarde"},
         {"Select a snapshot to verify or restore it.",
          "Sélectionnez une sauvegarde pour la vérifier ou la restaurer."},
         {"Preview checks every file before creating the destination.",
          "L'aperçu vérifie chaque fichier avant de créer la destination."},
         {"Cleanup removes only objects unused by every retained snapshot. Inspect again if the "
          "storage changes.",
          "Le nettoyage supprime uniquement les objets inutilisés par toutes les sauvegardes "
          "conservées. Relancez l'analyse si le stockage change."},
         {"Unrecognized entries are preserved.", "Les entrées non reconnues sont conservées."},
         {"Git exchange", "Échange Git"},
         {"Network", "Réseau"},
         {"Engine", "Moteur"},
         {"Open engine settings", "Configurer le moteur"},
         {"Configure Syncthing in the Engine view.", "Configurez Syncthing dans la vue Moteur."},
         {"Managed instance", "Instance gérée"},
         {"Existing instance", "Instance existante"},
         {"Syncthing executable", "Exécutable Syncthing"},
         {"API port", "Port de l'API"},
         {"Sync port", "Port de synchronisation"},
         {"Start with CySynck", "Démarrer avec CySynck"},
         {"Local connections only", "Connexions locales uniquement"},
         {"API address", "Adresse de l'API"},
         {"API key", "Clé API"},
         {"Save connection", "Enregistrer la connexion"},
         {"Start engine", "Démarrer le moteur"},
         {"Stop owned engine", "Arrêter le moteur géré"},
         {"Running", "En cours d'exécution"},
         {"Stopped or unreachable", "Arrêté ou inaccessible"},
         {"Controlled by this app", "Contrôlé par cette application"},
         {"External controller", "Contrôleur externe"},
         {"Devices and sharing", "Appareils et partage"},
         {"Device ID", "Identifiant de l'appareil"},
         {"Device name", "Nom de l'appareil"},
         {"Device address", "Adresse de l'appareil"},
         {"Add device", "Ajouter l'appareil"},
         {"Refresh devices", "Actualiser les appareils"},
         {"Share selected project", "Partager le projet sélectionné"},
         {"Read engine log", "Lire le journal du moteur"},
         {"Register this project, then add and authorize each device on both sides.",
          "Inscrivez ce projet, puis ajoutez et autorisez chaque appareil des deux côtés."},
         {"CySynck uses a separate configuration directory. Choose an installed Syncthing "
          "executable.",
          "CySynck utilise un dossier de configuration séparé. Choisissez un exécutable Syncthing "
          "installé."},
         {"Activity", "Activité"},
         {"Command line", "Ligne de commande"},
         {"Settings", "Paramètres"},
         {"Application", "Application"},
         {"Start at login", "Démarrage à l'ouverture de session"},
         {"Not registered", "Non inscrit"},
         {"Registered", "Inscrit"},
         {"Needs attention", "À vérifier"},
         {"Register at login", "Inscrire au démarrage"},
         {"Update login entry", "Mettre à jour l'inscription"},
         {"Remove login entry", "Retirer l'inscription"},
         {"Login entry saved", "Inscription au démarrage enregistrée"},
         {"Login entry removed", "Inscription au démarrage retirée"},
         {"Another file occupies this login entry. Inspect the path before registering.",
          "Un autre fichier occupe cette entrée de démarrage. Vérifiez le chemin avant de "
          "l'inscrire."},
         {"Register this profile for your next graphical login. The desktop starts in background; "
          "your operating system may ask for permission or disable startup entries.",
          "Inscrivez ce profil pour votre prochaine ouverture de session graphique. L'application "
          "démarre en arrière-plan ; le système peut demander une autorisation ou désactiver des "
          "entrées de démarrage."},
         {"Register again after moving the app or changing its startup plugins. Removing this "
          "entry keeps the current app and your data.",
          "Réinscrivez l'application après l'avoir déplacée ou avoir changé ses extensions de "
          "démarrage. Le retrait de cette entrée conserve l'application en cours et vos données."},
         {"Notification area", "Zone de notification"},
         {"Show CySynck", "Ouvrir CySynck"},
         {"Quit CySynck", "Quitter CySynck"},
         {"Back up all projects", "Sauvegarder tous les projets"},
         {"Close window to notification area", "Fermer la fenêtre vers la zone de notification"},
         {"Start desktop in background", "Démarrer l'application en arrière-plan"},
         {"Hide window", "Masquer la fenêtre"},
         {"Available", "Disponible"},
         {"Unavailable", "Indisponible"},
         {"Notification area disabled for this launch",
          "Zone de notification désactivée pour ce lancement"},
         {"The Windows notification area is unavailable",
          "La zone de notification Windows est indisponible"},
         {"The macOS status bar is unavailable", "La barre de menus macOS est indisponible"},
         {"Built without Ayatana AppIndicator support",
          "Compilé sans prise en charge d'Ayatana AppIndicator"},
         {"No graphical GTK session is available", "Aucune session graphique GTK n'est disponible"},
         {"No StatusNotifier host is connected; the window stays accessible",
          "Aucun panneau StatusNotifier n'est connecté ; la fenêtre reste accessible"},
         {"Cannot create the notification window",
          "Impossible de créer la fenêtre de notification"},
         {"Cannot create an application indicator",
          "Impossible de créer l'indicateur d'application"},
         {"Synchronization and scheduled backups continue while the window is hidden. Use Quit "
          "CySynck to stop this app and its owned engine.",
          "La synchronisation et les sauvegardes planifiées continuent lorsque la fenêtre est "
          "masquée. Quitter CySynck arrête cette application et son moteur géré."},
         {"Without a notification area, the window stays visible and closing it exits the app.",
          "Sans zone de notification, la fenêtre reste visible et sa fermeture quitte "
          "l'application."},
         {"This controls the next launch. It does not register the app at system login.",
          "Ce réglage concerne le prochain lancement. Il n'inscrit pas l'application au démarrage "
          "du système."},
         {"Name", "Nom"},
         {"Directory", "Dossier"},
         {"Mode", "Mode"},
         {"Project ID", "Identifiant du projet"},
         {"Exchange directory", "Dossier d'échange"},
         {"Interval (minutes)", "Intervalle (minutes)"},
         {"Snapshots to keep", "Sauvegardes à conserver"},
         {"Backup before sync", "Sauvegarder avant synchronisation"},
         {"Create", "Créer"},
         {"Cancel", "Annuler"},
         {"Save", "Enregistrer"},
         {"Refresh", "Actualiser"},
         {"Ready", "Prêt"},
         {"Working...", "Opération en cours..."},
         {"Theme", "Thème"},
         {"Language", "Langue"},
         {"Compare", "Comparer"},
         {"Create backup", "Créer une sauvegarde"},
         {"Latest snapshot", "Dernière sauvegarde"},
         {"Baseline", "Référence"},
         {"Destination", "Destination"},
         {"Restore to new directory", "Restaurer dans un nouveau dossier"},
         {"Register with Syncthing", "Ajouter à Syncthing"},
         {"Request scan", "Demander une analyse"},
         {"Check engine", "Vérifier le moteur"},
         {"Pause", "Mettre en pause"},
         {"Resume", "Reprendre"},
         {"No projects yet", "Aucun projet"},
         {"Select a project", "Sélectionnez un projet"},
         {"Choose a directory to start.", "Choisissez un dossier pour commencer."},
         {"Choose a project in the sidebar.", "Choisissez un projet dans le panneau latéral."},
         {"No backup yet", "Aucune sauvegarde"},
         {"Not checked", "Non vérifié"},
         {"Paused", "En pause"},
         {"State", "État"},
         {"Export committed branch", "Exporter la branche commitée"},
         {"Inspect Git", "Examiner Git"},
         {"Import tracking branch", "Importer la branche de suivi"},
         {"VPN identity", "Identité VPN"},
         {"VPN address", "Adresse VPN"},
         {"UDP endpoint", "Point d'accès UDP"},
         {"Configure identity", "Configurer l'identité"},
         {"Preview VPN config", "Afficher la configuration VPN"},
         {"Connect VPN", "Connecter le VPN"},
         {"Disconnect VPN", "Déconnecter le VPN"},
         {"Publish public profile", "Publier le profil public"},
         {"Discover peers", "Découvrir les appareils"},
         {"Trust public key", "Autoriser la clé publique"},
         {"I verified this key", "J'ai vérifié cette clé"},
         {"Direct chat", "Tchat direct"},
         {"Peer ID", "Identifiant de l'appareil"},
         {"Message", "Message"},
         {"Relative file path", "Chemin relatif du fichier"},
         {"Send message", "Envoyer le message"},
         {"Send file", "Envoyer le fichier"},
         {"Start receiver", "Démarrer la réception"},
         {"Stop receiver", "Arrêter la réception"},
         {"Run", "Exécuter"},
         {"Open Syncthing Web", "Ouvrir Syncthing Web"},
         {"Two-way sync", "Synchronisation bidirectionnelle"},
         {"Send only", "Envoi seul"},
         {"Receive only", "Réception seule"},
         {"Backup only", "Sauvegarde seule"},
         {"Set CYSYNCK_SYNCTHING_API_KEY before synchronizing.",
          "Définissez CYSYNCK_SYNCTHING_API_KEY avant de synchroniser."},
         {"Run a comparison against a snapshot.", "Lancez une comparaison avec une sauvegarde."},
         {"Restore verifies SHA-256 and creates a new directory.",
          "La restauration vérifie SHA-256 et crée un nouveau dossier."},
         {"Git imports create tracking refs. Review and merge using Git.",
          "Les imports Git créent des références de suivi. Examinez-les puis "
          "fusionnez avec Git."},
         {"Use Git exchange mode for this view.", "Utilisez le mode Échange Git pour cette vue."},
         {"WireGuard requires its official tools and administrator/root rights.",
          "WireGuard exige ses outils officiels et les droits "
          "administrateur/root."},
         {"A reachable UDP endpoint or IPv6 is needed. Automatic NAT traversal "
          "is pending.",
          "Un accès UDP joignable ou IPv6 est nécessaire. La traversée NAT "
          "automatique reste à développer."},
         {"Direct chat/files need an active VPN, receivers and a shared "
          "CYSYNCK_MESH_TOKEN.",
          "Le tchat et les fichiers directs exigent un VPN actif, des récepteurs "
          "et le même CYSYNCK_MESH_TOKEN."},
         {"0 disables scheduling. Keep the desktop or server running.",
          "0 désactive la planification. Gardez l'application ou le serveur "
          "actif."},
         {"Use a separate exchange directory and the same project ID on each "
          "peer.",
          "Utilisez un dossier d'échange séparé et le même identifiant de projet "
          "sur chaque appareil."},
         {"Syncthing handles continuous transfers. This requests a rescan.",
          "Syncthing gère les transferts continus. Ce bouton demande une "
          "nouvelle analyse."},
         {"CLI commands use the same C++ core. Quote paths with spaces.",
          "Les commandes CLI utilisent le même cœur C++. Mettez les chemins avec "
          "espaces entre guillemets."}});
}
class App {
    cysynck::Core &core;
    cysynck::desktop::LoginStartup &startup;
    std::future<Json> pending;
    std::function<void(const Json &)> callback;
    Json overview, events,
        snapshots = Json::array(), changed = Json::array(), diff, git_state, network_state,
        incoming = Json::array(), messages = Json::array(), engine_state, engine_devices,
        startup_state, backup_check, restore_preview, storage_state, conflict_state, conflict_diff,
        resolution_preview, resolution_preview_input, resolution_history, received_state,
        doctor_state, service_preview, service_preview_input, transfer_state, transfer_limits_state,
        git_branches, git_review_state, git_review_diff, git_storage_state;
    std::set<std::string> git_storage_selected;
    bool git_storage_current = false;
    std::string view = "Overview", selected, theme = "mid-grey", language = "en", error, output,
                query, baseline, comparison_snapshot, destination, restore_id, compare_left,
                compare_right, selected_conflict, merged_content, selected_resolution,
                resolution_destination, selected_received, received_destination, received_verified,
                command = "project list";
    bool create = false, before = true, storage_inspected = false, conflicts_loaded = false;
    int comparison_source = 0, resolution_choice = 0;
    bool history_loaded = false;
    bool received_loaded = false;
    bool git_loaded = false;
    bool reuse_blocks = true;
    bool send_legacy = false;
    bool include_git_lfs = true;
    bool git_lfs_policy = true;
    std::string git_lfs_storage;
    bool transfer_limits_dirty = false;
    int transfer_file_mib = 1024, transfer_inbox_mib = 4096, transfer_outbox_mib = 4096;
    int mode = 0, interval = 60, keep = 30;
    std::string name, root, exchange, shared_id;
    std::string vpn_name, vpn_address = "10.77.0.1", vpn_endpoint, chat_peer, chat_text, send_path;
    std::string engine_executable, engine_url = "http://127.0.0.1:8384", engine_key, device_id,
                                   device_name, device_address = "dynamic";
    int engine_mode = 0, engine_port = 8385, sync_port = 22001;
    bool engine_auto = false, engine_local = false;
    bool close_to_tray = true, start_hidden = false, tray_available = false, quit_requested = false,
         hide_requested = false;
    std::string tray_reason;
    std::string service_executable, service_target_state, service_destination;
    int service_platform = 0, service_port = 8484;
    std::unordered_map<std::string, bool> verified;
    c::DiffDocument document;
    c::DiffState diff_state;
    c::DiffDocument conflict_document;
    c::DiffState conflict_diff_state;
    c::DiffDocument git_document;
    c::DiffState git_diff_state;
    std::unique_ptr<cysynck::HttpServer> receiver;
    std::thread receiver_thread;
    std::chrono::steady_clock::time_point refreshed = std::chrono::steady_clock::now();

  public:
    App(cysynck::Core &value, cysynck::desktop::LoginStartup &login) : core(value), startup(login) {
#ifndef _WIN32
        service_target_state = cysynck::path_string(core.state());
#endif
#ifdef __APPLE__
        service_platform = 1;
#endif
        overview = core.request("GET", "/api/overview");
        events = core.request("GET", "/api/events");
        startup_state = startup.status();
        engine_state = core.request("GET", "/api/engine/status");
        auto engine_config = engine_state.at("config");
        engine_mode = engine_config.value("kind", "external") == "managed" ||
                              !engine_config.value("key_configured", false)
                          ? 0
                          : 1;
        engine_executable = engine_config.value("executable", "");
        engine_url = engine_config.value("url", engine_url);
        engine_port = engine_config.value("port", engine_port);
        sync_port = engine_config.value("listen_port", sync_port);
        engine_auto = engine_config.value("auto_start", false);
        engine_local = engine_config.value("loopback_only", false);
        auto settings = core.request("GET", "/api/settings");
        view = settings.value("ui_view", "Overview");
        selected = settings.value("ui_project", "");
        theme = settings.value("ui_theme", "mid-grey");
        language = settings.value("ui_language", "en");
        if (language != "en" && language != "fr")
            language = "en";
        close_to_tray = settings.value("ui_close_to_tray", true);
        start_hidden = settings.value("ui_start_hidden", false);
        localize();
        c::Config config;
        config.themeKey = theme.c_str();
        config.density = c::Density::comfortable;
        c::apply(config);
        c::setLocale(language.c_str());
        c::loadFonts();
        if (!project().is_null()) {
            auto p = project();
            interval = p["backup_interval_minutes"];
            keep = p["backup_keep"];
            before = p["backup_before_sync"];
            reuse_blocks = p.value("backup_storage", "auto") == "auto";
            git_lfs_policy = p.value("git_lfs", true);
            git_lfs_storage = p.value("git_lfs_storage", "");
            try {
                snapshots = core.request("GET", "/api/projects/" + selected + "/snapshots");
            } catch (const std::exception &e) {
                error = e.what();
            }
        }
    }
    ~App() {
        if (pending.valid())
            pending.wait();
        if (receiver)
            receiver->stop();
        if (receiver_thread.joinable())
            receiver_thread.join();
        try {
            core.request("PUT", "/api/settings",
                         {{"ui_view", view},
                          {"ui_project", selected},
                          {"ui_theme", theme},
                          {"ui_language", language},
                          {"ui_close_to_tray", close_to_tray},
                          {"ui_start_hidden", start_hidden}});
        } catch (...) {
        }
    }
    bool busy() const { return pending.valid(); }
    Json project() {
        for (auto &p : overview["projects"])
            if (p["id"] == selected)
                return p;
        return nullptr;
    }
    void submit(std::function<Json()> work, std::function<void(const Json &)> done = {}) {
        if (busy())
            return;
        error.clear();
        callback = std::move(done);
        pending = std::async(std::launch::async, std::move(work));
    }
    void refresh() {
        auto id = selected;
        submit(
            [this, id] {
                Json list = Json::array();
                std::string snapshot_error;
                if (!id.empty()) {
                    try {
                        list = core.request("GET", "/api/projects/" + id + "/snapshots");
                    } catch (const std::exception &e) {
                        snapshot_error = e.what();
                    }
                }
                return Json{{"overview", core.request("GET", "/api/overview")},
                            {"events", core.request("GET", "/api/events")},
                            {"engine", core.request("GET", "/api/engine/status")},
                            {"startup", startup.status()},
                            {"snapshots", list},
                            {"snapshot_error", snapshot_error}};
            },
            [this, id](const Json &result) {
                overview = result["overview"];
                events = result["events"];
                engine_state = result["engine"];
                startup_state = result["startup"];
                if (id == selected) {
                    snapshots = result["snapshots"];
                    if (!result.at("snapshot_error").get<std::string>().empty())
                        error = result.at("snapshot_error");
                } else
                    refresh();
            });
        refreshed = std::chrono::steady_clock::now();
    }
    void action(const std::string &action, Json data = Json::object(),
                std::function<void(const Json &)> done = {}) {
        auto id = selected;
        submit([this, id, action,
                data] { return core.request("POST", "/api/projects/" + id + "/" + action, data); },
               [this, done, id](const Json &result) {
                   output = result.dump(2);
                   if (done && id == selected)
                       done(result);
                   refresh();
               });
    }
    void net(const std::string &action, Json data = Json::object(),
             std::function<void(const Json &)> done = {}) {
        submit(
            [this, action, data] {
                return core.request(action == "status" || action == "transfer-limits" ? "GET"
                                                                                      : "POST",
                                    "/api/network/" + action, data);
            },
            [this, done](const Json &result) {
                output = result.dump(2);
                if (done)
                    done(result);
            });
    }
    void select(const std::string &id) {
        selected = id;
        view = "Changes";
        changed = Json::array();
        diff = nullptr;
        incoming = Json::array();
        git_loaded = false;
        messages = Json::array();
        received_state = nullptr;
        transfer_state = nullptr;
        received_loaded = false;
        selected_received.clear();
        received_destination.clear();
        received_verified.clear();
        baseline.clear();
        comparison_snapshot.clear();
        snapshots = Json::array();
        restore_id.clear();
        backup_check = restore_preview = nullptr;
        git_state = nullptr;
        git_branches = git_review_state = git_review_diff = nullptr;
        git_storage_state = nullptr;
        git_storage_selected.clear();
        git_storage_current = false;
        conflict_state = conflict_diff = nullptr;
        resolution_preview = resolution_preview_input = resolution_history = nullptr;
        history_loaded = false;
        selected_resolution.clear();
        conflicts_loaded = false;
        selected_conflict.clear();
        compare_left.clear();
        compare_right.clear();
        auto p = project();
        if (p.contains("resolution_pending"))
            view = "Recovery";
        interval = p["backup_interval_minutes"];
        keep = p["backup_keep"];
        before = p["backup_before_sync"];
        reuse_blocks = p.value("backup_storage", "auto") == "auto";
        git_lfs_policy = p.value("git_lfs", true);
        git_lfs_storage = p.value("git_lfs_storage", "");
        refresh();
    }
    void set_view(const std::string &s) { view = s; }
    void set_theme(const std::string &s) {
        theme = s;
        c::setTheme(s.c_str());
    }
    void set_language(const std::string &s) {
        language = s == "fr" ? "fr" : "en";
        c::setLocale(language.c_str());
    }
    void first_project() {
        if (!overview["projects"].empty())
            select(overview["projects"][0]["id"]);
    }
    bool should_hide_on_close() const {
        return cysynck::desktop::close_hides(close_to_tray, tray_available);
    }
    bool starts_hidden() const { return start_hidden; }
    bool take_quit() { return std::exchange(quit_requested, false); }
    bool take_hide() { return std::exchange(hide_requested, false); }
    void set_tray_state(bool available, std::string reason) {
        tray_available = available;
        tray_reason = std::move(reason);
    }
    cysynck::desktop::TrayLabels tray_labels() const {
        auto color = c::vec(c::palette().accent);
        return {
            c::tr("Show CySynck"),
            c::tr("Open Syncthing Web"),
            c::tr("Back up all projects"),
            c::tr("Quit CySynck"),
            "CySynck / " + std::to_string(overview.at("projects").size()) + " " +
                c::tr("Projects") + " / " +
                c::tr(engine_state.value("running", false) ? "Running" : "Stopped or unreachable"),
            static_cast<unsigned char>(color.x * 255),
            static_cast<unsigned char>(color.y * 255),
            static_cast<unsigned char>(color.z * 255)};
    }
    void backup_all() {
        submit(
            [this] {
                Json result = Json::array();
                auto projects = core.request("GET", "/api/overview").at("projects");
                for (const auto &p : projects) {
                    if (p.value("paused", false))
                        continue;
                    auto id = p.at("id").get<std::string>();
                    try {
                        result.push_back(
                            {{"project", id},
                             {"backup", core.request("POST", "/api/projects/" + id + "/backup")}});
                    } catch (const std::exception &error) {
                        result.push_back({{"project", id}, {"error", error.what()}});
                    }
                }
                return result;
            },
            [this](const Json &result) {
                output = result.dump(2);
                refresh();
            });
    }
    void poll() {
        if (pending.valid() &&
            pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto done = std::move(callback);
            try {
                auto result = pending.get();
                if (done)
                    done(result);
            } catch (const std::exception &e) {
                error = e.what();
                c::notify(error.c_str(), c::Tone::error, 8);
            }
        }
        if (!busy() && std::chrono::steady_clock::now() - refreshed >
                           std::chrono::seconds(view == "Engine" ? 2 : 10))
            refresh();
    }
    void draw() {
        auto vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("CySynck", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("navigation", c::dp(245, 0));
        c::heading("CySynck", 1);
        c::textDim("%s / C++20", cysynck::version);
        c::space(12);
        if (c::sidebarItem("", c::trLabel("Overview").c_str(), view == "Overview"))
            view = "Overview";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##search", &query);
        c::sidebarGroup(c::tr("Projects").c_str());
        for (auto &p : overview["projects"]) {
            auto n = p["name"].get<std::string>();
            if (!query.empty() && n.find(query) == std::string::npos)
                continue;
            if (c::sidebarItem("", (n + "###" + p["id"].get<std::string>()).c_str(),
                               p["id"] == selected))
                select(p["id"]);
        }
        if (button("New project"))
            create = true;
        c::space(14);
        for (auto page :
             {"Changes", "Conflicts", "Backups", "Git exchange", "Engine", "Network", "Activity",
              "Storage", "Diagnostics", "Services", "Command line", "Settings", "Application"})
            if (c::sidebarItem("", c::trLabel(page).c_str(),
                               view == page ||
                                   (std::string(page) == "Conflicts" && view == "Recovery") ||
                                   (std::string(page) == "Network" &&
                                    (view == "Received files" || view == "Direct transfers"))))
                view = page;
        c::space(12);
        int lang = language == "fr";
        c::textDim("%s", c::tr("Language").c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##language", &lang, "English\0Français\0"))
            set_language(lang ? "fr" : "en");
        const char *themes[] = {"mid-grey", "light-grey", "amber-night", "violet", "navy"};
        int theme_index = 0;
        for (int i = 0; i < 5; ++i)
            if (theme == themes[i])
                theme_index = i;
        c::textDim("%s", c::tr("Theme").c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##theme", &theme_index, themes, 5))
            set_theme(themes[theme_index]);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("workspace", ImVec2(0, 0));
        c::heading(c::tr(view.c_str()).c_str(), 1);
        auto p = project();
        c::textDim("%s", (view == "Storage" || view == "Diagnostics" || view == "Services")
                             ? c::tr("All projects").c_str()
                         : !p.is_null() && view != "Overview" ? p["name"].get<std::string>().c_str()
                                                              : c::tr("Projects").c_str());
        if (!error.empty())
            c::callout(c::Tone::error, error.c_str());
        c::badge(c::tr(busy() ? "Working..." : "Ready").c_str(),
                 busy() ? c::Tone::info : c::Tone::success);
        c::space(8);
        if (view == "Overview")
            overview_view();
        else if (view == "Engine")
            engine_view();
        else if (view == "Application")
            application_view();
        else if (view == "Diagnostics")
            diagnostics_view();
        else if (view == "Services")
            services_view();
        else if (view == "Storage")
            storage_view();
        else if (view == "Network")
            network_view();
        else if (view == "Activity") {
            for (auto &e : events) {
                c::textDim("%s / %s", e["created"].get<std::string>().substr(0, 19).c_str(),
                           e["kind"].get<std::string>().c_str());
                ImGui::TextWrapped("%s", e["message"].get<std::string>().c_str());
                c::space(6);
            }
        } else if (view == "Command line")
            cli_view();
        else if (p.is_null())
            c::emptyState(c::tr("Select a project").c_str(),
                          c::tr("Choose a project in the sidebar.").c_str());
        else if (view == "Changes")
            changes_view();
        else if (view == "Conflicts")
            conflicts_view();
        else if (view == "Recovery")
            recovery_view();
        else if (view == "Received files")
            received_view();
        else if (view == "Direct transfers")
            transfers_view();
        else if (view == "Backups")
            backup_view();
        else if (view == "Git exchange")
            git_view();
        else if (view == "Settings")
            settings_view(p);
        ImGui::EndChild();
        create_view();
        ImGui::End();
        c::renderNotifications();
    }
    void diagnostics_view() {
        ImGui::BeginDisabled(busy());
        bool inspect = button("Inspect diagnostics");
        ImGui::SameLine();
        bool probe = button("Test tools and connection", true);
        ImGui::EndDisabled();
        if ((inspect || probe || doctor_state.is_null()) && !busy())
            submit(
                [this, probe] {
                    return core.request(probe ? "POST" : "GET", "/api/doctor",
                                        {{"probe_tools", probe}, {"probe_engine", probe}});
                },
                [this](const Json &result) { doctor_state = result; });
        if (doctor_state.is_null())
            return;
        c::textDim("%d %s / %d %s", doctor_state.at("errors").get<int>(), c::tr("Errors").c_str(),
                   doctor_state.at("warnings").get<int>(), c::tr("Warnings").c_str());
        for (const auto &row : doctor_state.at("checks")) {
            auto label = row.at("label").get<std::string>();
            c::beginCard(
                row.at("id").get<std::string>().c_str(),
                (row.at("details").contains("project") ? label : c::tr(label.c_str())).c_str());
            auto status = row.at("status").get<std::string>();
            c::badge(c::tr(row.at("message").get<std::string>().c_str()).c_str(),
                     status == "error"     ? c::Tone::error
                     : status == "warning" ? c::Tone::warning
                                           : c::Tone::success);
            ImGui::TextWrapped("%s", c::tr(row.at("hint").get<std::string>().c_str()).c_str());
            if (row.at("details").contains("path"))
                ImGui::TextWrapped("%s", row.at("details").at("path").get<std::string>().c_str());
            if (row.at("details").contains("version"))
                text(row.at("details").at("version"));
            c::endCard();
            c::space(6);
        }
    }
    Json service_input() const {
        return {{"platform", service_platform ? "macos" : "linux"},
                {"executable", service_executable},
                {"target_state", service_target_state},
                {"port", service_port}};
    }
    void services_view() {
        ImGui::TextWrapped(
            "%s",
            c::tr("Review the target paths and port. Export does not install or start a service.")
                .c_str());
        ImGui::BeginDisabled(busy());
        if (c::beginProperties("service-fields")) {
            c::property(c::tr("Target platform").c_str());
            ImGui::Combo("##service-platform", &service_platform,
                         "Linux (systemd)\0macOS (launchd)\0");
            field("CLI on target", service_executable);
            field("State on target", service_target_state);
            c::property(c::tr("API port").c_str());
            ImGui::InputInt("##service-port", &service_port);
            c::endProperties();
        }
        if (!service_preview.is_null() && service_preview_input != service_input())
            service_preview = nullptr;
        ImGui::BeginDisabled(service_executable.empty() || service_target_state.empty());
        if (button("Prepare service", true)) {
            auto input = service_input();
            service_preview = nullptr;
            submit([this, input] { return core.request("POST", "/api/service/plan", input); },
                   [this, input](const Json &result) {
                       if (input == service_input()) {
                           service_preview_input = input;
                           service_preview = result;
                       }
                   });
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!service_preview.is_null()) {
            text(service_preview.at("filename"));
            ImGui::BeginChild("service-preview", ImVec2(0, c::dp(240)), true,
                              ImGuiWindowFlags_HorizontalScrollbar);
            text(service_preview.at("contents"));
            ImGui::EndChild();
        }
        ImGui::BeginDisabled(busy());
        if (c::beginProperties("service-export")) {
            field("Destination", service_destination);
            c::endProperties();
        }
        ImGui::BeginDisabled(service_preview.is_null() || service_destination.empty());
        if (button("Export service files", true)) {
            auto input = service_input();
            input["plan"] = service_preview.at("plan");
            input["destination"] = service_destination;
            submit([this, input] { return core.request("POST", "/api/service/export", input); },
                   [this](const Json &result) {
                       output = result.dump(2);
                       c::notify(c::tr("Service files exported").c_str(), c::Tone::success);
                   });
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", c::tr("Open the exported README on the target machine for "
                                       "installation and removal commands.")
                                     .c_str());
    }
    void application_view() {
        c::beginCard("desktop-preferences", c::tr("Notification area").c_str());
        c::badge(c::tr(tray_available ? "Available" : "Unavailable").c_str(),
                 tray_available ? c::Tone::success : c::Tone::warning);
        ImGui::TextWrapped(
            "%s", c::tr("Synchronization and scheduled backups continue while the window is "
                        "hidden. Use Quit CySynck to stop this app and its owned engine.")
                      .c_str());
        if (!tray_available) {
            c::callout(c::Tone::warning, c::tr("Without a notification area, the window stays "
                                               "visible and closing it exits the app.")
                                             .c_str());
            c::textDim("%s", c::tr(tray_reason.c_str()).c_str());
        }
        bool preferences_changed = false;
        ImGui::BeginDisabled(busy());
        if (c::beginProperties("desktop-fields")) {
            c::property(c::tr("Close window to notification area").c_str());
            preferences_changed |= ImGui::Checkbox("##close-to-tray", &close_to_tray);
            c::property(c::tr("Start desktop in background").c_str());
            preferences_changed |= ImGui::Checkbox("##start-hidden", &start_hidden);
            c::endProperties();
        }
        ImGui::EndDisabled();
        if (preferences_changed) {
            auto close = close_to_tray, hidden = start_hidden;
            submit([this, close, hidden] {
                return core.request("PUT", "/api/settings",
                                    {{"ui_close_to_tray", close}, {"ui_start_hidden", hidden}});
            });
        }
        c::textDim(
            "%s",
            c::tr("This controls the next launch. It does not register the app at system login.")
                .c_str());
        ImGui::BeginDisabled(!tray_available);
        if (button("Hide window"))
            hide_requested = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Back up all projects"))
            backup_all();
        ImGui::SameLine();
        if (button("Quit CySynck"))
            quit_requested = true;
        c::endCard();
        c::space(10);
        c::beginCard("login-startup", c::tr("Start at login").c_str());
        bool registered = startup_state.value("registered", false);
        bool owned = startup_state.value("owned", false);
        bool current = startup_state.value("up_to_date", false);
        c::badge(c::tr(!registered ? "Not registered"
                       : current   ? "Registered"
                                   : "Needs attention")
                     .c_str(),
                 current ? c::Tone::success : c::Tone::warning);
        ImGui::TextWrapped("%s", c::tr("Register this profile for your next graphical login. The "
                                       "desktop starts in background; your operating system may "
                                       "ask for permission or disable startup entries.")
                                     .c_str());
        ImGui::PushTextWrapPos(0);
        c::textDim("%s", startup_state.value("entry", "").c_str());
        ImGui::PopTextWrapPos();
        if (registered && !owned)
            c::callout(
                c::Tone::warning,
                c::tr(
                    "Another file occupies this login entry. Inspect the path before registering.")
                    .c_str());
        if (startup_state.contains("error"))
            c::callout(c::Tone::error, startup_state.at("error").get<std::string>().c_str());
        ImGui::BeginDisabled(busy() || (registered && !owned));
        if (button(registered ? "Update login entry" : "Register at login", !registered)) {
            submit([this] { return startup.enable(); },
                   [this](const Json &result) {
                       startup_state = result;
                       c::notify(c::tr("Login entry saved").c_str(), c::Tone::success);
                   });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(busy() || !owned);
        if (button("Remove login entry")) {
            submit([this] { return startup.disable(); },
                   [this](const Json &result) {
                       startup_state = result;
                       c::notify(c::tr("Login entry removed").c_str(), c::Tone::success);
                   });
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0);
        c::textDim("%s", c::tr("Register again after moving the app or changing its startup "
                               "plugins. Removing this entry keeps the current app and your data.")
                             .c_str());
        ImGui::PopTextWrapPos();
        c::endCard();
        if (!output.empty()) {
            c::space(10);
            text(output);
        }
    }
    void overview_view() {
        c::beginCard("summary");
        c::heading((std::to_string(overview["projects"].size()) + "  " + c::tr("Projects")).c_str(),
                   2);
        c::textDim("Peer: %.12s / C++ core + CyUI", overview["peer_id"].get<std::string>().c_str());
        c::endCard();
        c::space(12);
        if (!overview["syncthing_configured"].get<bool>())
            c::callout(c::Tone::warning, c::tr("Configure Syncthing in the Engine view.").c_str());
        if (button("New project", true))
            create = true;
        ImGui::SameLine();
        if (button("Open engine settings"))
            view = "Engine";
        ImGui::SameLine();
        if (button("Open Syncthing Web"))
            open_engine_web();
        if (overview["projects"].empty()) {
            c::emptyState(c::tr("No projects yet").c_str(),
                          c::tr("Choose a directory to start.").c_str());
            return;
        }
        if (ImGui::BeginTable("projects", 4,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_Resizable)) {
            for (auto title : {"Name", "Mode", "Latest snapshot", "State"})
                ImGui::TableSetupColumn(c::tr(title).c_str());
            ImGui::TableHeadersRow();
            for (auto &p : overview["projects"]) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(
                        (p["name"].get<std::string>() + "###row" + p["id"].get<std::string>())
                            .c_str()))
                    select(p["id"]);
                ImGui::TableSetColumnIndex(1);
                text(p["mode"]);
                ImGui::TableSetColumnIndex(2);
                text(p["latest_snapshot"].is_null()
                         ? c::tr("No backup yet")
                         : p["latest_snapshot"]["created"].get<std::string>().substr(0, 19));
                ImGui::TableSetColumnIndex(3);
                c::badge(c::tr(p.contains("resolution_pending") ? "Needs recovery"
                               : p.contains("backup_error")     ? "Integrity check failed"
                               : p["paused"].get<bool>()        ? "Paused"
                                                                : "Not checked")
                             .c_str(),
                         p.contains("backup_error") || p.contains("resolution_pending")
                             ? c::Tone::error
                             : c::Tone::neutral);
            }
            ImGui::EndTable();
        }
    }
    void open_engine_web() {
        auto url = overview["syncthing_url"].get<std::string>();
#ifdef _WIN32
        auto wide = fs::u8path(url);
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        submit([url] {
            cysynck::process({"open", url});
            return Json::object();
        });
#else
        submit([url] {
            cysynck::process({"xdg-open", url});
            return Json::object();
        });
#endif
    }
    void engine_call(const std::string &action, Json data = Json::object()) {
        submit(
            [this, action, data] {
                auto result = core.request(
                    action == "status" || action == "devices" || action == "log" ? "GET" : "POST",
                    "/api/engine/" + action, data);
                return Json{{"result", result},
                            {"status", core.request("GET", "/api/engine/status")}};
            },
            [this, action](const Json &result) {
                engine_state = result.at("status");
                if (action == "devices")
                    engine_devices = result.at("result");
                if (action == "configure" || action == "connect")
                    engine_devices = nullptr;
                if (action == "connect")
                    engine_key.clear();
                output = action == "log" ? result.at("result").value("text", "")
                                         : result.at("result").dump(2);
                refresh();
            });
    }
    void engine_view() {
        bool running = engine_state.value("running", false);
        c::beginCard("engine-status", "Syncthing");
        c::badge(c::tr(running ? "Running" : "Stopped or unreachable").c_str(),
                 running ? c::Tone::success : c::Tone::warning);
        if (running) {
            c::textDim("%s / %s", engine_state.value("version", "").c_str(),
                       c::tr(engine_state.value("owned", false) ? "Controlled by this app"
                                                                : "External controller")
                           .c_str());
            text(engine_state.value("device_id", ""));
            if (button("Refresh devices"))
                engine_call("devices");
            ImGui::SameLine();
        }
        if (button("Refresh"))
            engine_call("status");
        ImGui::SameLine();
        if (button("Start engine", !running))
            engine_call("start");
        ImGui::SameLine();
        ImGui::BeginDisabled(!engine_state.value("owned", false));
        if (button("Stop owned engine"))
            engine_call("stop");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Read engine log"))
            engine_call("log");
        ImGui::SameLine();
        if (button("Open Syncthing Web"))
            open_engine_web();
        c::endCard();
        c::space(10);
        c::beginCard("engine-connection", c::tr("Settings").c_str());
        auto managed = c::tr("Managed instance"), external = c::tr("Existing instance");
        const char *modes[] = {managed.c_str(), external.c_str()};
        ImGui::Combo(c::trLabel("Mode").c_str(), &engine_mode, modes, 2);
        if (engine_mode == 0)
            ImGui::TextWrapped("%s", c::tr("CySynck uses a separate configuration directory. "
                                           "Choose an installed Syncthing executable.")
                                         .c_str());
        if (c::beginProperties("engine-properties")) {
            if (engine_mode == 0) {
                field("Syncthing executable", engine_executable);
                c::property(c::tr("API port").c_str());
                ImGui::InputInt("##engine-port", &engine_port);
                c::property(c::tr("Sync port").c_str());
                ImGui::InputInt("##sync-port", &sync_port);
                c::property(c::tr("Start with CySynck").c_str());
                ImGui::Checkbox("##auto-engine", &engine_auto);
                c::property(c::tr("Local connections only").c_str());
                ImGui::Checkbox("##local-engine", &engine_local);
            } else {
                field("API address", engine_url);
                c::property(c::tr("API key").c_str());
                ImGui::InputText("##engine-api-key", &engine_key, ImGuiInputTextFlags_Password);
            }
            c::endProperties();
        }
        if (button("Save connection", true)) {
            if (engine_mode == 0)
                engine_call("configure", {{"executable", engine_executable},
                                          {"port", engine_port},
                                          {"listen_port", sync_port},
                                          {"auto_start", engine_auto},
                                          {"loopback_only", engine_local}});
            else
                engine_call("connect", {{"url", engine_url}, {"api_key", engine_key}});
        }
        c::endCard();
        c::space(10);
        if (running) {
            c::beginCard("engine-devices", c::tr("Devices and sharing").c_str());
            ImGui::TextWrapped(
                "%s",
                c::tr("Register this project, then add and authorize each device on both sides.")
                    .c_str());
            if (c::beginProperties("device-fields")) {
                field("Device ID", device_id);
                field("Device name", device_name);
                field("Device address", device_address);
                c::endProperties();
            }
            if (button("Add device"))
                engine_call(
                    "add-device",
                    {{"device_id", device_id}, {"name", device_name}, {"address", device_address}});
            auto selected_project = project();
            if (!selected_project.is_null()) {
                ImGui::SameLine();
                if (button("Share selected project"))
                    engine_call("share", {{"project", selected}, {"device_id", device_id}});
                c::textDim("%s", selected_project.at("name").get<std::string>().c_str());
            }
            if (engine_devices.contains("devices"))
                for (const auto &device : engine_devices.at("devices")) {
                    auto id = device.at("deviceID").get<std::string>();
                    auto label = device.value("name", "") + " / " + id;
                    if (ImGui::Selectable(label.c_str(), device_id == id))
                        device_id = id;
                }
            c::endCard();
        }
        if (!output.empty()) {
            c::space(10);
            ImGui::TextWrapped("%s", output.c_str());
        }
    }
    void changes_view() {
        const auto snapshot_label = c::tr("Snapshot"), files_label = c::tr("Two project files");
        const char *sources[] = {snapshot_label.c_str(), files_label.c_str()};
        if (ImGui::Combo(c::trLabel("Source").c_str(), &comparison_source, sources, 2))
            diff = nullptr;
        if (comparison_source == 1) {
            if (c::beginProperties("compare-files")) {
                field("Left relative path", compare_left);
                field("Right relative path", compare_right);
                c::endProperties();
            }
            ImGui::BeginDisabled(compare_left.empty() || compare_right.empty());
            if (button("Compare", true))
                action("compare", {{"left", compare_left}, {"right", compare_right}},
                       [this](const Json &r) {
                           if (comparison_source == 1)
                               set_comparison(r, diff, document);
                       });
            ImGui::EndDisabled();
            if (!diff.is_null())
                comparison_view("files-comparison", diff, document, diff_state);
            return;
        }
        auto current = baseline.empty() ? c::tr("Latest snapshot") : baseline.substr(0, 12);
        if (ImGui::BeginCombo(c::trLabel("Baseline").c_str(), current.c_str())) {
            if (ImGui::Selectable(c::tr("Latest snapshot").c_str(), baseline.empty()))
                baseline.clear();
            for (auto &s : snapshots)
                if (ImGui::Selectable((s["created"].get<std::string>() + " / " +
                                       s["id"].get<std::string>().substr(0, 8))
                                          .c_str(),
                                      baseline == s["id"].get<std::string>()))
                    baseline = s["id"];
            ImGui::EndCombo();
        }
        if (button("Compare", true))
            action("changes", {{"snapshot", baseline}}, [this](const Json &r) {
                changed = r["files"];
                comparison_snapshot =
                    r["baseline"].is_null() ? "" : r["baseline"].get<std::string>();
                diff = nullptr;
            });
        ImGui::SameLine();
        if (button("Create backup"))
            action("backup");
        ImGui::BeginChild("files", c::dp(285, 0), ImGuiChildFlags_Borders);
        if (changed.empty())
            ImGui::TextWrapped("%s", c::tr("Run a comparison against a snapshot.").c_str());
        for (auto &item : changed) {
            auto path = item["path"].get<std::string>();
            if (ImGui::Selectable(
                    (item["status"].get<std::string>().substr(0, 1) + "  " + path).c_str()))
                action("diff", {{"path", path}, {"snapshot", comparison_snapshot}},
                       [this](const Json &r) {
                           if (comparison_source == 0)
                               set_comparison(r, diff, document);
                       });
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("diff", ImVec2(0, 0));
        if (!diff.is_null()) {
            c::textDim("%s", diff["path"].get<std::string>().c_str());
            comparison_view("compare", diff, document, diff_state);
        } else
            c::emptyState(c::tr("Changes").c_str(),
                          c::tr("Run a comparison against a snapshot.").c_str());
        ImGui::EndChild();
    }
    void set_comparison(const Json &result, Json &value, c::DiffDocument &doc) {
        value = result;
        if (!value.at("binary").get<bool>())
            doc.setTexts(value.at("before"), value.at("after"),
                         c::codeLanguageForPath(value.at("path").get<std::string>().c_str()));
    }
    void comparison_view(const char *id, const Json &value, c::DiffDocument &doc,
                         c::DiffState &state) {
        if (value.contains("left"))
            ImGui::TextWrapped("%s  /  %s", value.at("left").get<std::string>().c_str(),
                               value.at("right").get<std::string>().c_str());
        if (ImGui::CollapsingHeader(c::trLabel("Comparison details").c_str())) {
            for (auto side : {"before_info", "after_info"}) {
                const auto &info = value.at(side);
                c::textDim(
                    "%s: %llu B",
                    c::tr(std::string(side) == "before_info" ? "Left side" : "Right side").c_str(),
                    static_cast<unsigned long long>(info.at("bytes").get<uintmax_t>()));
                if (!info.at("sha256").is_null())
                    text(info.at("sha256").get<std::string>());
            }
        }
        if (value.at("binary").get<bool>()) {
            c::callout(c::Tone::info,
                       c::tr(value.at("message").get<std::string>().c_str()).c_str());
            return;
        }
        c::textDim("+%llu / -%llu",
                   static_cast<unsigned long long>(value.at("added_lines").get<size_t>()),
                   static_cast<unsigned long long>(value.at("removed_lines").get<size_t>()));
        ImGui::SameLine();
        ImGui::BeginDisabled(value.at("patch").get_ref<const std::string &>().empty());
        if (button("Copy patch")) {
            ImGui::SetClipboardText(value.at("patch").get_ref<const std::string &>().c_str());
            c::notify(c::tr("Patch copied").c_str(), c::Tone::success);
        }
        ImGui::EndDisabled();
        if (value.at("approximate").get<bool>())
            c::callout(c::Tone::info,
                       c::tr("The patch replaces a large changed block; it is not a minimal diff.")
                           .c_str());
        if (value.at("identical").get<bool>())
            c::badge(c::tr("No differences").c_str(), c::Tone::success);
        c::diffViewer(id, state, doc, ImVec2(0, 0));
    }
    void open_conflict(const std::string &path) {
        selected_conflict = path;
        conflict_diff = nullptr;
        resolution_preview = resolution_preview_input = nullptr;
        resolution_choice = 0;
        merged_content.clear();
        action("conflict-diff", {{"path", path}}, [this, path](const Json &r) {
            if (path == selected_conflict) {
                set_comparison(r, conflict_diff, conflict_document);
                if (!r.at("binary").get<bool>()) {
                    auto content =
                        r.at(r.at("before_info").at("exists").get<bool>() ? "before" : "after")
                            .get<std::string>();
                    for (size_t i = 0; i < content.size(); ++i)
                        if (content[i] != '\r' || i + 1 == content.size() || content[i + 1] != '\n')
                            merged_content += content[i];
                }
            }
        });
    }
    void conflicts_view() {
        ImGui::TextWrapped(
            "%s",
            c::tr("Select a conflict copy to compare its contents with the current file.").c_str());
        bool refresh_conflicts = button("Find conflicts", true);
        ImGui::SameLine();
        if (button("Recovery history")) {
            view = "Recovery";
            history_loaded = false;
        }
        if ((refresh_conflicts || !conflicts_loaded) && !busy()) {
            conflicts_loaded = true;
            action("conflicts", Json::object(), [this](const Json &r) {
                conflict_state = r;
                conflict_diff = nullptr;
                selected_conflict.clear();
                if (!r.at("files").empty())
                    open_conflict(r.at("files").at(0).at("path"));
            });
        }
        if (conflict_state.is_null())
            return;
        if (conflict_state.at("truncated").get<bool>())
            c::callout(c::Tone::warning,
                       c::tr("Only the first 10000 conflict copies are listed.").c_str());
        if (!conflict_state.at("errors").empty() &&
            ImGui::CollapsingHeader(c::trLabel("Diagnostics").c_str()))
            for (const auto &e : conflict_state.at("errors"))
                ImGui::TextWrapped("%s: %s", e.at("path").get<std::string>().c_str(),
                                   e.at("error").get<std::string>().c_str());
        c::space(8);
        ImGui::BeginChild("conflict-list", c::dp(320, 0), ImGuiChildFlags_Borders);
        if (conflict_state.at("files").empty())
            ImGui::TextWrapped("%s", c::tr("No conflict copies found").c_str());
        for (const auto &item : conflict_state.at("files")) {
            auto path = item.at("path").get<std::string>();
            ImGui::BeginDisabled(busy());
            if (ImGui::Selectable((item.at("original").get<std::string>() + "###" + path).c_str(),
                                  selected_conflict == path))
                open_conflict(path);
            ImGui::EndDisabled();
            c::textDim("%s / %s", item.at("timestamp").get<std::string>().c_str(),
                       item.at("modified_by").get<std::string>().c_str());
            if (!item.at("current_exists").get<bool>())
                c::textDim("%s", c::tr("Original file is missing").c_str());
            c::space(5);
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("conflict-preview", ImVec2(0, 0));
        if (!conflict_diff.is_null()) {
            resolution_view();
            comparison_view("conflict-diff", conflict_diff, conflict_document, conflict_diff_state);
        }
        ImGui::EndChild();
    }
    Json resolution_data() const {
        Json data = {{"path", selected_conflict},
                     {"choice", resolution_choice == 0   ? "current"
                                : resolution_choice == 1 ? "conflict"
                                                         : "merged"}};
        data["expected_current"] = conflict_diff.at("before_info");
        data["expected_conflict"] = conflict_diff.at("after_info");
        if (resolution_choice == 2)
            data["content"] = merged_content;
        return data;
    }
    void resolution_view() {
        c::beginCard("resolution", c::tr("Resolve conflict").c_str());
        ImGui::TextWrapped(
            "%s",
            c::tr("Both source versions are preserved. The prepared choice changes the local file.")
                .c_str());
        const auto current =
            c::tr(conflict_diff.at("before_info").at("exists").get<bool>() ? "Keep current version"
                                                                           : "Keep deletion");
        const auto other = c::tr("Use conflict copy"), manual = c::tr("Manual merge");
        const char *choices[] = {current.c_str(), other.c_str(), manual.c_str()};
        ImGui::BeginDisabled(busy());
        if (ImGui::BeginCombo(c::trLabel("Choice").c_str(), choices[resolution_choice])) {
            for (int i = 0; i < 3; ++i) {
                ImGui::BeginDisabled(i == 2 && conflict_diff.at("binary").get<bool>());
                if (ImGui::Selectable(choices[i], resolution_choice == i))
                    resolution_choice = i;
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (resolution_choice == 2) {
            ImGui::InputTextMultiline("##merged-content", &merged_content, c::dp(-1, 160),
                                      ImGuiInputTextFlags_AllowTabInput);
            c::textDim("%s", c::tr("Manual editor uses LF line endings. Maximum 512 KiB.").c_str());
        }
        auto p = project();
        if (!p.at("paused").get<bool>() && button("Pause"))
            action("pause", {{"paused", true}});
        ImGui::TextWrapped("%s",
                           c::tr("The project must be paused before applying a choice. Resume "
                                 "synchronization explicitly after checking the result.")
                               .c_str());
        auto input = resolution_data();
        ImGui::BeginDisabled(resolution_choice == 2 && merged_content.size() > 512 * 1024);
        if (button("Prepare resolution", true))
            action("resolution-plan", input, [this, input](const Json &r) {
                resolution_preview = r;
                resolution_preview_input = input;
            });
        ImGui::EndDisabled();
        ImGui::SameLine();
        bool prepared = !resolution_preview.is_null() && resolution_preview_input == input;
        ImGui::BeginDisabled(!prepared || !p.at("paused").get<bool>() ||
                             (prepared && !resolution_preview.at("space_available").get<bool>()) ||
                             p.contains("resolution_pending"));
        if (button("Apply resolution")) {
            input["plan"] = resolution_preview.at("plan");
            action("resolve-conflict", input, [this](const Json &r) {
                selected_resolution = r.at("id");
                history_loaded = false;
                conflicts_loaded = false;
                conflict_diff = resolution_preview = nullptr;
                view = "Recovery";
                c::notify(c::tr("Resolution applied").c_str(), c::Tone::success);
            });
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (prepared) {
            c::textDim("%s / %llu B", resolution_preview.at("original").get<std::string>().c_str(),
                       static_cast<unsigned long long>(
                           resolution_preview.at("result").at("bytes").get<uintmax_t>()));
            if (!resolution_preview.at("space_available").get<bool>())
                c::callout(c::Tone::warning,
                           c::tr("Not enough free space for recovery copies.").c_str());
            if (!resolution_preview.at("result").at("sha256").is_null()) {
                ImGui::PushTextWrapPos(0);
                c::textDim("SHA-256: %s",
                           resolution_preview.at("result").at("sha256").get<std::string>().c_str());
                ImGui::PopTextWrapPos();
            }
        }
        c::endCard();
        c::space(8);
    }
    void recovery_view() {
        if (button("Back to conflicts"))
            view = "Conflicts";
        ImGui::SameLine();
        bool reload = button("Refresh");
        if ((reload || !history_loaded) && !busy()) {
            history_loaded = true;
            action("resolutions", Json::object(), [this](const Json &r) {
                resolution_history = r;
                if (selected_resolution.empty() && !r.at("records").empty())
                    selected_resolution = r.at("records").at(0).at("id");
            });
        }
        if (resolution_history.is_null())
            return;
        auto p = project();
        if (p.contains("resolution_pending")) {
            c::callout(c::Tone::warning, c::tr("Needs recovery").c_str());
            if (!p.at("paused").get<bool>() && button("Pause"))
                action("pause", {{"paused", true}});
        }
        if (resolution_history.at("records").empty())
            c::emptyState(c::tr("No resolution history").c_str(), "");
        for (const auto &record : resolution_history.at("records")) {
            auto label = record.at("created").get<std::string>() + " / " +
                         record.at("original").get<std::string>();
            if (ImGui::Selectable((label + "###" + record.at("id").get<std::string>()).c_str(),
                                  record.at("id") == selected_resolution))
                selected_resolution = record.at("id");
            if (record.at("id") != selected_resolution)
                continue;
            c::beginCard("recovery-record");
            auto phase = record.at("phase").get<std::string>();
            bool attention = record.at("needs_attention");
            c::badge(c::tr(attention                 ? "Needs recovery"
                           : phase == "applied"      ? "Applied"
                           : phase == "acknowledged" ? "Reviewed"
                                                     : "Rolled back")
                         .c_str(),
                     attention ? c::Tone::warning : c::Tone::success);
            text(record.at("id"));
            ImGui::TextWrapped("%s: %s", c::tr("Recovery directory").c_str(),
                               record.at("recovery_directory").get<std::string>().c_str());
            if (record.contains("error"))
                ImGui::TextWrapped("%s", record.at("error").get<std::string>().c_str());
            ImGui::TextWrapped(
                "%s", c::tr("Recovery copies are exported to a new separate directory.").c_str());
            if (c::beginProperties("recovery-destination")) {
                field("Destination", resolution_destination);
                c::endProperties();
            }
            ImGui::BeginDisabled(busy() || resolution_destination.empty());
            if (button("Export recovery copies"))
                action("resolution-export", {{"resolution", selected_resolution},
                                             {"destination", resolution_destination}});
            ImGui::EndDisabled();
            if (p.value("resolution_pending", std::string{}) == selected_resolution) {
                ImGui::TextWrapped("%s",
                                   c::tr("After restoring or checking the working files, mark this "
                                         "recovery as reviewed. The backups are kept.")
                                       .c_str());
                ImGui::BeginDisabled(busy() || !p.at("paused").get<bool>());
                if (button("Mark recovery as reviewed"))
                    action("resolution-acknowledge", {{"resolution", selected_resolution}},
                           [this](const Json &) { history_loaded = false; });
                ImGui::EndDisabled();
            }
            c::endCard();
        }
        for (const auto &e : resolution_history.at("errors"))
            ImGui::TextWrapped("%s: %s", e.at("path").get<std::string>().c_str(),
                               e.at("error").get<std::string>().c_str());
    }
    void backup_view() {
        if (button("Create backup", true))
            action("backup");
        c::space(12);
        if (snapshots.empty())
            c::emptyState(c::tr("No backup yet").c_str(),
                          c::tr("Select a snapshot to verify or restore it.").c_str());
        if (restore_id.empty() && !snapshots.empty())
            restore_id = snapshots[0].at("id");
        for (auto &s : snapshots) {
            auto label = s["created"].get<std::string>() + " | " +
                         std::to_string(s["file_count"].get<size_t>()) + " " + c::tr("Files") +
                         " | " + std::to_string(s["bytes"].get<uintmax_t>() / 1024) + " KiB | " +
                         s["reason"].get<std::string>();
            if (ImGui::Selectable((label + "###" + s["id"].get<std::string>()).c_str(),
                                  restore_id == s["id"].get<std::string>())) {
                restore_id = s["id"];
                backup_check = restore_preview = nullptr;
            }
            if (s.at("id") == restore_id && s.contains("block_storage")) {
                const auto &stats = s.at("block_storage");
                c::textDim("%s: %llu KiB / %s: %llu KiB", c::tr("Block data written").c_str(),
                           static_cast<unsigned long long>(
                               stats.at("new_block_bytes").get<uintmax_t>() / 1024),
                           c::tr("Block data reused").c_str(),
                           static_cast<unsigned long long>(
                               stats.at("reused_block_bytes").get<uintmax_t>() / 1024));
            }
        }
        if (!restore_id.empty()) {
            c::space(12);
            c::beginCard("restore", c::tr("Restore to new directory").c_str());
            ImGui::TextWrapped(
                "%s", c::tr("Preview checks every file before creating the destination.").c_str());
            if (button("Verify backup"))
                action("verify", {{"snapshot", restore_id}},
                       [this](const Json &r) { backup_check = r; });
            if (!backup_check.is_null() && backup_check.at("snapshot") == restore_id)
                integrity_result(backup_check);
            if (c::beginProperties("restore-fields")) {
                c::property(c::tr("Snapshot").c_str());
                text(restore_id);
                field("Destination", destination);
                c::endProperties();
            }
            ImGui::BeginDisabled(destination.empty());
            if (button("Preview restoration")) {
                auto requested = destination;
                action("restore-plan", {{"snapshot", restore_id}, {"destination", requested}},
                       [this, requested](const Json &r) {
                           restore_preview = r;
                           restore_preview["input_destination"] = requested;
                       });
            }
            ImGui::EndDisabled();
            bool preview_current = !restore_preview.is_null() &&
                                   restore_preview.at("snapshot") == restore_id &&
                                   restore_preview.at("input_destination") == destination;
            ImGui::SameLine();
            ImGui::BeginDisabled(!preview_current || !restore_preview.value("ok", false));
            if (button("Restore to new directory", true))
                action("restore", {{"snapshot", restore_id}, {"destination", destination}},
                       [this](const Json &) { restore_preview = nullptr; });
            ImGui::EndDisabled();
            if (preview_current) {
                integrity_result(restore_preview);
                ImGui::BeginChild("restore-entries", c::dp(0, 175), ImGuiChildFlags_Borders);
                const auto &entries = restore_preview.at("entries");
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(entries.size()));
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                        const auto &entry = entries.at(i);
                        text(entry.at("path").get<std::string>() + "  /  " +
                             std::to_string(entry.at("size").get<uintmax_t>()) + " B");
                    }
                ImGui::EndChild();
            }
            c::endCard();
        }
    }
    void integrity_result(const Json &result) {
        bool ok = result.at("ok");
        c::badge(c::tr(ok ? "Integrity verified" : "Integrity check failed").c_str(),
                 ok ? c::Tone::success : c::Tone::error);
        c::textDim("%s / %llu / %.2f MiB / %s", c::tr("Files").c_str(),
                   static_cast<unsigned long long>(result.at("files").get<uintmax_t>()),
                   result.at("bytes").get<double>() / 1048576.0,
                   result.at("checked_at").get<std::string>().c_str());
        for (const auto &issue : result.at("issues"))
            ImGui::TextWrapped("%s: %s", issue.at("path").get<std::string>().c_str(),
                               issue.at("error").get<std::string>().c_str());
    }
    void storage_view() {
        ImGui::TextWrapped("%s", c::tr("Cleanup removes only objects unused by every retained "
                                       "snapshot. Inspect again if the storage changes.")
                                     .c_str());
        bool inspect = button("Inspect storage", true);
        if ((inspect || !storage_inspected) && !busy()) {
            storage_inspected = true;
            submit([this] { return core.request("GET", "/api/storage"); },
                   [this](const Json &r) { storage_state = r; });
        }
        if (storage_state.is_null())
            return;
        c::space(10);
        c::beginCard("storage-summary", c::tr("Storage").c_str());
        if (c::beginProperties("storage-totals")) {
            for (auto [key, label] : {std::pair{"stored_bytes", "Stored"},
                                      {"reclaimable_bytes", "Reclaimable"},
                                      {"deduplicated_bytes", "Deduplicated data"}}) {
                c::property(c::tr(label).c_str());
                if (std::string(key) == "deduplicated_bytes" &&
                    !storage_state.at("deduplication_complete").get<bool>()) {
                    text(c::tr("Unavailable"));
                    continue;
                }
                auto bytes = storage_state.at(key).get<uintmax_t>();
                text(bytes < 1024 ? std::to_string(bytes) + " B"
                                  : std::to_string(bytes / 1024) + " KiB");
            }
            for (auto [key, label] : {std::pair{"snapshots", "Backups"},
                                      {"unused_objects", "Unused objects"},
                                      {"missing_objects", "Missing objects"},
                                      {"block_objects", "Block objects"},
                                      {"chunked_files", "Chunked file versions"}}) {
                c::property(c::tr(label).c_str());
                text(std::to_string(storage_state.at(key).get<uintmax_t>()));
            }
            c::endProperties();
        }
        if (!storage_state.at("can_prune").get<bool>())
            c::callout(c::Tone::error, c::tr("Cleanup blocked").c_str());
        for (const auto &issue : storage_state.at("errors"))
            ImGui::TextWrapped("%s: %s", issue.value("path", "").c_str(),
                               issue.at("error").get<std::string>().c_str());
        if (!storage_state.at("ignored_entries").empty())
            c::textDim("%s", c::tr("Unrecognized entries are preserved.").c_str());
        ImGui::BeginDisabled(busy() || !storage_state.at("can_prune").get<bool>() ||
                             storage_state.at("unused_objects") == 0);
        if (button("Free unused space")) {
            auto token = storage_state.at("plan");
            storage_state = nullptr;
            submit(
                [this, token] {
                    auto result = core.request("POST", "/api/storage/prune",
                                               {{"apply", true}, {"plan", token}});
                    return Json{{"result", result},
                                {"status", core.request("GET", "/api/storage")}};
                },
                [this](const Json &r) {
                    output = r.at("result").dump(2);
                    storage_state = r.at("status");
                    if (r.at("result").at("failures").empty())
                        c::notify(c::tr("Storage cleanup complete").c_str(), c::Tone::success);
                    else
                        error = r.at("result").at("failures").dump(2);
                });
        }
        ImGui::EndDisabled();
        c::endCard();
    }
    void git_view() {
        if (project()["mode"] != "git-bundles") {
            c::callout(c::Tone::info, c::tr("Use Git exchange mode for this view.").c_str());
            return;
        }
        c::callout(
            c::Tone::info,
            c::tr("Git and LFS imports preserve the working tree. Review and merge using Git.")
                .c_str());
        auto load = [this] {
            action("inbox", Json::object(), [this](const Json &r) {
                incoming = r;
                git_loaded = true;
            });
        };
        if (!git_loaded && !busy())
            load();
        if (git_branches.is_null() && !busy())
            action("git-branches", Json::object(), [this](const Json &r) { git_branches = r; });
        ImGui::Checkbox(c::trLabel("Include Git LFS history").c_str(), &include_git_lfs);
        if (button("Export committed branch", true))
            action("export", {{"lfs", include_git_lfs}}, [this](const Json &) {
                git_loaded = false;
                git_storage_current = false;
            });
        ImGui::SameLine();
        if (button("Inspect Git"))
            action("git-status", Json::object(), [this](const Json &r) { git_state = r; });
        ImGui::SameLine();
        if (button("Refresh")) {
            git_branches = git_review_state = git_review_diff = nullptr;
            git_storage_current = false;
            load();
        }
        c::beginCard("git-storage", c::tr("Git exchange storage").c_str());
        ImGui::TextWrapped("%s", c::tr("Select exports to remove. Peers can no longer import "
                                       "removed exports. Local Git data is preserved.")
                                     .c_str());
        if (button("Preview Git cleanup")) {
            auto selection = git_storage_selected;
            action("git-storage", {{"exports", selection}}, [this, selection](const Json &r) {
                git_storage_state = r;
                git_storage_current = selection == git_storage_selected;
            });
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!git_storage_current || git_storage_state.is_null() ||
                             !git_storage_state.value("can_prune", false));
        if (button("Apply Git cleanup")) {
            auto query = Json{{"exports", git_storage_selected},
                              {"apply", true},
                              {"plan", git_storage_state.at("plan")}};
            git_storage_current = false;
            action("git-storage", query, [this](const Json &r) {
                git_storage_state = nullptr;
                git_storage_selected.clear();
                git_loaded = false;
                if (!r.at("failures").empty())
                    error = r.at("failures").dump(2);
            });
        }
        ImGui::EndDisabled();
        if (!git_storage_state.is_null()) {
            ImGui::BeginDisabled(busy());
            for (const auto &entry : git_storage_state.at("exports")) {
                auto id = entry.at("id").get<std::string>();
                bool remove = git_storage_selected.contains(id);
                auto label = entry.at("branch").get<std::string>() + " / " + id;
                if (ImGui::Checkbox(label.c_str(), &remove)) {
                    if (remove)
                        git_storage_selected.insert(id);
                    else
                        git_storage_selected.erase(id);
                    git_storage_current = false;
                }
            }
            ImGui::EndDisabled();
            text(c::tr("Unused LFS objects") + ": " +
                 git_storage_state.at("unused_objects").dump());
            text(c::tr("Reclaimable bytes") + ": " +
                 git_storage_state.at("reclaimable_bytes").dump());
            for (const auto &issue : git_storage_state.at("errors"))
                text(issue.dump());
        }
        c::endCard();
        if (!git_branches.is_null()) {
            c::heading(c::tr("Imported branches").c_str(), 3);
            for (const auto &branch : git_branches) {
                auto label = branch.at("peer").get<std::string>() + "/" +
                             branch.at("branch").get<std::string>();
                ImGui::PushID(label.c_str());
                text(label);
                if (button("Review branch"))
                    action("git-review",
                           {{"peer", branch.at("peer")}, {"branch", branch.at("branch")}},
                           [this](const Json &r) {
                               git_review_state = r;
                               git_review_diff = nullptr;
                           });
                ImGui::PopID();
            }
        }
        if (!git_review_state.is_null()) {
            const auto labels =
                std::map<std::string, std::string>{{"identical", "Branches are identical"},
                                                   {"fast_forward", "Fast-forward is possible"},
                                                   {"local_ahead", "Local branch is ahead"},
                                                   {"diverged", "Branches have diverged"},
                                                   {"unrelated", "Histories are unrelated"}};
            c::callout(c::Tone::info,
                       c::tr(labels.at(git_review_state.at("relation")).c_str()).c_str());
            text(c::tr("Local commits") + ": " + git_review_state.at("local_only_commits").dump() +
                 " / " + c::tr("Peer commits") + ": " +
                 git_review_state.at("peer_only_commits").dump());
            for (const auto &file : git_review_state.at("files")) {
                auto path = file.at("path").get<std::string>();
                ImGui::PushID(path.c_str());
                text(file.at("status").get<std::string>() + " / " + path);
                if (button("Review file")) {
                    Json query;
                    for (auto key : {"peer", "branch", "base", "head"})
                        query[key] = git_review_state.at(key);
                    query["path"] = path;
                    action("git-review-file", query, [this, query](const Json &r) {
                        if (!git_review_state.is_null() &&
                            git_review_state.at("base") == query.at("base") &&
                            git_review_state.at("head") == query.at("head"))
                            set_comparison(r, git_review_diff, git_document);
                    });
                }
                ImGui::PopID();
            }
            if (!git_review_diff.is_null()) {
                text(git_review_diff.at("path"));
                if (git_review_diff.value("reason", "") == "lfs_pointer")
                    c::textDim(
                        "%s", c::tr("LFS pointer comparison; asset bytes are not loaded.").c_str());
                comparison_view("git-diff", git_review_diff, git_document, git_diff_state);
            }
        }
        if (!git_state.is_null())
            text(git_state.dump(2));
        for (auto &b : incoming)
            if (b.contains("branch")) {
                ImGui::PushID(b["id"].get<std::string>().c_str());
                c::beginCard("bundle");
                c::heading((b["branch"].get<std::string>() + " / " +
                            b["head"].get<std::string>().substr(0, 10))
                               .c_str(),
                           3);
                c::textDim("%s", b["peer"].get<std::string>().c_str());
                if (b.contains("lfs_status")) {
                    c::textDim("%s: %zu / %llu B", c::tr("Git LFS objects").c_str(),
                               b.at("lfs_status").at("objects").get<size_t>(),
                               static_cast<unsigned long long>(
                                   b.at("lfs_status").at("bytes").get<uintmax_t>()));
                    if (!b.at("lfs_status").at("complete").get<bool>())
                        c::textDim("%s", c::tr("Waiting for Git LFS objects").c_str());
                }
                ImGui::BeginDisabled(b.contains("lfs_status") &&
                                     !b.at("lfs_status").at("complete").get<bool>());
                if (button("Import tracking branch"))
                    action("import", {{"peer", b["peer"]}, {"bundle", b["id"]}},
                           [this](const Json &) {
                               git_branches = git_review_state = git_review_diff = nullptr;
                           });
                ImGui::EndDisabled();
                c::endCard();
                ImGui::PopID();
            }
    }
    void settings_view(const Json &p) {
        c::beginCard("config", c::tr("Settings").c_str());
        if (c::beginProperties("settings")) {
            for (auto key : {"id", "root", "mode", "exchange_path"}) {
                c::property(key);
                text(p[key]);
            }
            c::property(c::tr("Interval (minutes)").c_str());
            ImGui::InputInt("##interval", &interval);
            c::property(c::tr("Snapshots to keep").c_str());
            ImGui::InputInt("##keep", &keep);
            c::property(c::tr("Backup before sync").c_str());
            c::toggleSwitch("##before", &before);
            c::property(c::tr("Reuse blocks for large files").c_str());
            c::toggleSwitch("##reuse-blocks", &reuse_blocks);
            if (p.at("mode") == "git-bundles") {
                c::property(c::tr("Use Git LFS during synchronization").c_str());
                c::toggleSwitch("##git-lfs-policy", &git_lfs_policy);
                field("Approved LFS storage directory", git_lfs_storage);
            }
            c::endProperties();
        }
        c::textDim("%s",
                   c::tr("0 disables scheduling. Keep the desktop or server running.").c_str());
        ImGui::TextWrapped(
            "%s", c::tr("Blocks of 4 MiB are reused for files from 8 MiB to 256 GiB.").c_str());
        if (p.at("mode") == "git-bundles")
            ImGui::TextWrapped("%s",
                               c::tr("Leave empty for the default Git cache. A custom directory "
                                     "must already exist and match Git lfs.storage.")
                                   .c_str());
        if (button("Save", true)) {
            auto id = selected;
            auto data = Json{{"backup_interval_minutes", interval},
                             {"backup_keep", keep},
                             {"backup_storage", reuse_blocks ? "auto" : "whole"},
                             {"backup_before_sync", before}};
            if (p.at("mode") == "git-bundles") {
                data["git_lfs"] = git_lfs_policy;
                data["git_lfs_storage"] = git_lfs_storage;
            }
            submit([this, id, data] { return core.request("PUT", "/api/projects/" + id, data); },
                   [this](const Json &) { refresh(); });
        }
        c::endCard();
        c::space(12);
        c::beginCard("engine", "Syncthing");
        c::textDim(
            "%s", c::tr("Syncthing handles continuous transfers. This requests a rescan.").c_str());
        if (button("Register with Syncthing"))
            action("register");
        ImGui::SameLine();
        if (button("Request scan"))
            action("sync");
        ImGui::SameLine();
        if (button("Check engine"))
            action("status");
        ImGui::SameLine();
        if (button(p["paused"].get<bool>() ? "Resume" : "Pause"))
            action("pause", {{"paused", !p["paused"].get<bool>()}});
        text(output);
        c::endCard();
    }
    void received_view() {
        if (button("Back to network"))
            view = "Network";
        ImGui::SameLine();
        auto load = [this] {
            received_loaded = true;
            received_verified.clear();
            action("received-files", Json::object(), [this](const Json &result) {
                received_state = result;
                bool found = false;
                for (const auto &file : result.at("files"))
                    found |= file.at("id") == selected_received;
                if (!found)
                    selected_received = result.at("files").empty()
                                            ? ""
                                            : result.at("files")[0].at("id").get<std::string>();
            });
        };
        if (button("Refresh"))
            load();
        if (!received_loaded && !busy())
            load();
        c::space(8);
        c::callout(c::Tone::info,
                   c::tr("Export creates a new separate directory and preserves the received copy.")
                       .c_str());
        if (received_state.is_null())
            return;
        for (const auto &issue : received_state.at("errors"))
            ImGui::TextWrapped("%s: %s", issue.value("id", "").c_str(),
                               issue.at("error").get<std::string>().c_str());
        if (!received_state.at("unindexed").empty()) {
            c::callout(
                c::Tone::warning,
                c::tr("Unindexed inbox files are preserved and require manual review.").c_str());
            for (const auto &filename : received_state.at("unindexed"))
                text(filename);
        }
        if (received_state.at("files").empty()) {
            c::emptyState(c::tr("No received files").c_str(), "");
            return;
        }
        ImGui::BeginChild("received-list", c::dp(300, 0), ImGuiChildFlags_Borders);
        Json current;
        for (const auto &file : received_state.at("files")) {
            auto id = file.at("id").get<std::string>();
            if (ImGui::Selectable((file.at("name").get<std::string>() + "###" + id).c_str(),
                                  id == selected_received))
                selected_received = id;
            if (id == selected_received)
                current = file;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("received-detail", ImVec2(0, 0));
        if (!current.is_null()) {
            c::beginCard("received-item", current.at("name").get<std::string>().c_str());
            text(current.at("created").get<std::string>() + " / " +
                 current.at("peer").get<std::string>());
            text(std::to_string(current.at("bytes").get<uintmax_t>()) + " B");
            c::textMono("SHA-256: %s", current.at("sha256").get<std::string>().c_str());
            if (!current.at("available").get<bool>())
                c::callout(c::Tone::error, c::tr("Payload unavailable").c_str());
            if (received_verified == selected_received)
                c::badge(c::tr("Last verification succeeded").c_str(), c::Tone::success);
            ImGui::BeginDisabled(busy() || !current.at("available").get<bool>());
            if (button("Verify received file")) {
                auto id = selected_received;
                received_verified.clear();
                action("received-verify", {{"transfer", id}}, [this, id](const Json &) {
                    received_verified = id;
                    c::notify(c::tr("Received file verified").c_str(), c::Tone::success);
                });
            }
            if (c::beginProperties("received-export-fields")) {
                field("Destination", received_destination);
                c::endProperties();
            }
            ImGui::BeginDisabled(received_destination.empty());
            if (button("Export received file", true))
                action("received-export",
                       {{"transfer", selected_received}, {"destination", received_destination}},
                       [this](const Json &) {
                           c::notify(c::tr("Received file exported").c_str(), c::Tone::success);
                       });
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            c::endCard();
        }
        ImGui::EndChild();
    }
    void transfers_view() {
        if (button("Back to network"))
            view = "Network";
        auto id = selected;
        auto load = [this, id] {
            net("transfers", {{"project", id}}, [this, id](const Json &result) {
                if (selected == id)
                    transfer_state = result;
            });
        };
        c::space(8);
        if (transfer_limits_state.is_null() && !busy())
            net("transfer-limits", Json::object(), [this](const Json &result) {
                transfer_limits_state = result;
                if (!transfer_limits_dirty) {
                    transfer_file_mib = result.at("file_limit_mib");
                    transfer_inbox_mib = result.at("inbox_quota_mib");
                    transfer_outbox_mib = result.at("outbox_quota_mib");
                }
            });
        c::beginCard("transfer-limits", c::tr("Profile transfer limits").c_str());
        ImGui::TextWrapped(
            "%s",
            c::tr(
                "These limits apply to all projects. Existing transfers keep their reserved size.")
                .c_str());
        ImGui::BeginDisabled(busy() || transfer_limits_state.is_null());
        if (c::beginProperties("transfer-limit-fields")) {
            c::property(c::tr("File limit (MiB)").c_str());
            transfer_limits_dirty |= ImGui::InputInt("##transfer-file-mib", &transfer_file_mib);
            c::property(c::tr("Inbox quota (MiB)").c_str());
            transfer_limits_dirty |= ImGui::InputInt("##transfer-inbox-mib", &transfer_inbox_mib);
            c::property(c::tr("Outbox quota (MiB)").c_str());
            transfer_limits_dirty |= ImGui::InputInt("##transfer-outbox-mib", &transfer_outbox_mib);
            c::endProperties();
        }
        if (button("Save transfer limits"))
            net("transfer-configure",
                {{"file_limit_mib", transfer_file_mib},
                 {"inbox_quota_mib", transfer_inbox_mib},
                 {"outbox_quota_mib", transfer_outbox_mib}},
                [this](const Json &result) {
                    transfer_limits_state = result;
                    transfer_limits_dirty = false;
                });
        ImGui::EndDisabled();
        if (!transfer_limits_state.is_null()) {
            for (const auto &[key, title] : std::vector<std::pair<std::string, std::string>>{
                     {"inbox_bytes", "Inbox bytes"},
                     {"reserved_incoming_bytes", "Reserved incoming bytes"},
                     {"reserved_outgoing_bytes", "Reserved outgoing bytes"}})
                text(c::tr(title.c_str()) + ": " + transfer_limits_state.at(key).dump());
        }
        c::endCard();
        c::space(8);
        c::beginCard("transfers", c::tr("Direct transfers").c_str());
        c::textDim("%s", c::tr("Resume uses a frozen copy of the source. Both peers need an active "
                               "VPN and receiver.")
                             .c_str());
        if (button("Refresh transfers")) {
            transfer_limits_state = nullptr;
            transfer_state = nullptr;
        }
        if (transfer_state.is_null() && !busy())
            load();
        if (!transfer_state.is_null()) {
            if (transfer_state.at("files").empty())
                c::textDim("%s", c::tr("No direct transfers").c_str());
            for (const auto &file : transfer_state.at("files")) {
                auto transfer = file.at("id").get<std::string>();
                auto direction = file.at("direction").get<std::string>();
                ImGui::PushID((direction + transfer).c_str());
                c::heading(file.at("name").get<std::string>().c_str(), 3);
                text(file.at("peer").get<std::string>() + " / " + transfer);
                c::textDim(
                    "%s / %s",
                    c::tr(direction == "out" ? "Outgoing transfer" : "Incoming transfer").c_str(),
                    c::tr(file.at("delivered").get<bool>() ? "Delivered"
                                                           : "Waiting for continuation")
                        .c_str());
                auto bytes = file.at("bytes").get<uintmax_t>();
                auto done = file.at("acknowledged_bytes").get<uintmax_t>();
                auto caption = std::to_string(done) + " / " + std::to_string(bytes) + " B";
                ImGui::ProgressBar(bytes ? static_cast<float>(done) / static_cast<float>(bytes)
                                         : (file.at("delivered").get<bool>() ? 1.f : 0.f),
                                   ImVec2(-1, 0), caption.c_str());
                if (!file.value("error", "").empty())
                    c::textDim("%s", c::tr(file.value("error_code", "") == "unsupported_protocol"
                                               ? "This peer needs an update or legacy mode."
                                               : "Transfer interrupted. Check receiver, pairing "
                                                 "and VPN, then resume.")
                                         .c_str());
                auto doneAction = [this, id](const Json &) {
                    if (selected == id) {
                        transfer_state = nullptr;
                        transfer_limits_state = nullptr;
                    }
                };
                if (direction == "out" && !file.at("delivered").get<bool>()) {
                    if (button("Resume transfer"))
                        net("transfer-resume", {{"project", id}, {"transfer", transfer}},
                            doneAction);
                    ImGui::SameLine();
                }
                if (button("Remove local transfer"))
                    net("transfer-cancel",
                        {{"project", id}, {"transfer", transfer}, {"direction", direction}},
                        doneAction);
                ImGui::PopID();
            }
            for (const auto &issue : transfer_state.at("errors"))
                text(issue.dump());
            if (!transfer_state.at("files").empty())
                c::textDim("%s", c::tr("Removal clears local partial data or transfer history. "
                                       "Received files are kept; the other peer is unchanged.")
                                     .c_str());
        }
        c::endCard();
    }
    void network_view() {
        if (!project().is_null() && button("Open received files", true))
            view = "Received files";
        if (!project().is_null()) {
            ImGui::SameLine();
            if (button("Direct transfers"))
                view = "Direct transfers";
        }
        c::callout(c::Tone::info, c::tr("WireGuard requires its official tools and "
                                        "administrator/root rights.")
                                      .c_str());
        c::callout(c::Tone::warning, c::tr("A reachable UDP endpoint or IPv6 is needed. Automatic "
                                           "NAT traversal is pending.")
                                         .c_str());
        c::beginCard("identity", c::tr("VPN identity").c_str());
        if (c::beginProperties("vpn-fields")) {
            field("Name", vpn_name);
            field("VPN address", vpn_address);
            field("UDP endpoint", vpn_endpoint);
            c::endProperties();
        }
        if (button("Configure identity", true))
            net("configure",
                {{"name", vpn_name}, {"address", vpn_address}, {"endpoint", vpn_endpoint}});
        ImGui::SameLine();
        if (button("Preview VPN config"))
            net("status", Json::object(), [this](const Json &r) {
                network_state = r;
                if (!r["profile"].is_null()) {
                    vpn_name = r["profile"]["name"];
                    vpn_address = r["profile"]["address"];
                    vpn_endpoint = r["profile"]["endpoint"];
                }
            });
        if (!network_state.is_null()) {
            if (!network_state["profile"].is_null())
                c::textMono("%s",
                            network_state["profile"]["public_key"].get<std::string>().c_str());
            text(network_state["config_preview"]);
            c::textDim("%s", network_state["status_error"].get<std::string>().c_str());
        }
        if (button("Connect VPN"))
            net("up");
        ImGui::SameLine();
        if (button("Disconnect VPN"))
            net("down");
        c::endCard();
        if (project().is_null()) {
            c::emptyState(c::tr("Select a project").c_str(),
                          c::tr("Choose a project in the sidebar.").c_str());
            return;
        }
        c::space(8);
        c::beginCard("peers", project()["name"].get<std::string>().c_str());
        if (button("Publish public profile"))
            net("publish", {{"project", selected}});
        ImGui::SameLine();
        if (button("Discover peers"))
            net("discover", {{"project", selected}}, [this](const Json &r) {
                incoming = r["peers"];
                error = r["discovery_error"];
            });
        for (auto &candidate : incoming)
            if (candidate.contains("profile")) {
                auto p = candidate["profile"];
                auto id = p["peer_id"].get<std::string>();
                ImGui::PushID(id.c_str());
                c::heading((p["name"].get<std::string>() + " / " + p["address"].get<std::string>())
                               .c_str(),
                           3);
                c::textMono("%s", p["public_key"].get<std::string>().c_str());
                c::textDim("%s / %s", p["endpoint"].get<std::string>().c_str(),
                           candidate["syncthing_transport"].get<std::string>().c_str());
                ImGui::Checkbox(c::trLabel("I verified this key").c_str(), &verified[id]);
                if (verified[id] && button("Trust public key")) {
                    net("trust",
                        {{"project", selected}, {"profile", p}, {"expected_key", p["public_key"]}});
                    chat_peer = id;
                }
                ImGui::PopID();
            }
        c::endCard();
        c::space(8);
        c::beginCard("chat", c::tr("Direct chat").c_str());
        c::textDim("%s", c::tr("Direct chat/files need an active VPN, receivers "
                               "and a shared CYSYNCK_MESH_TOKEN.")
                             .c_str());
        if (button(receiver ? "Stop receiver" : "Start receiver")) {
            if (receiver) {
                receiver->stop();
                if (receiver_thread.joinable())
                    receiver_thread.join();
                receiver.reset();
            } else
                try {
                    auto p = core.request("GET", "/api/network/status").at("profile");
                    receiver = std::make_unique<cysynck::HttpServer>(core, true);
                    receiver->bind(p.at("address"), p.at("mesh_port"));
                    receiver_thread = std::thread([this] { receiver->listen(); });
                } catch (const std::exception &e) {
                    receiver.reset();
                    error = e.what();
                }
        }
        ImGui::SameLine();
        if (button("Refresh"))
            net("history", {{"project", selected}}, [this](const Json &r) { messages = r; });
        if (c::beginProperties("chat-fields")) {
            field("Peer ID", chat_peer);
            field("Message", chat_text);
            field("Relative file path", send_path);
            c::endProperties();
        }
        if (button("Send message"))
            net("chat", {{"project", selected}, {"peer", chat_peer}, {"text", chat_text}});
        ImGui::SameLine();
        if (button("Send file"))
            net("send-file",
                {{"project", selected},
                 {"peer", chat_peer},
                 {"path", send_path},
                 {"legacy", send_legacy}},
                [this, id = selected](const Json &) {
                    if (selected == id) {
                        transfer_state = nullptr;
                        transfer_limits_state = nullptr;
                    }
                });
        ImGui::Checkbox(c::trLabel("Legacy peer (single request)").c_str(), &send_legacy);
        for (auto &m : messages) {
            c::textDim("%s / %s", m["created"].get<std::string>().substr(0, 19).c_str(),
                       m["direction"].get<std::string>().c_str());
            text(m["body"].value("text", m["body"].value("name", std::string{})));
        }
        c::endCard();
    }
    void cli_view() {
        c::textDim("%s",
                   c::tr("CLI commands use the same C++ core. Quote paths with spaces.").c_str());
        c::textMono("project list | backup <id> | vpn status | help");
        ImGui::SetNextItemWidth(-c::dp(100));
        bool enter = ImGui::InputText("##command", &command, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (button("Run", true) || enter) {
            auto input = command;
            submit(
                [this, input] {
                    auto args = split(input);
                    if (!args.empty() && args[0] == "desktop")
                        return cysynck::desktop::startup_command(startup, args);
                    return cysynck::cli(core, args);
                },
                [this](const Json &r) {
                    output = r.dump(2);
                    refresh();
                });
        }
        ImGui::BeginChild("cli-output", ImVec2(0, 0), ImGuiChildFlags_Borders);
        text(output);
        ImGui::EndChild();
    }
    void create_view() {
        if (!c::beginModal(c::trLabel("New project").c_str(), &create, 660))
            return;
        if (c::beginProperties("create-fields")) {
            field("Name", name);
            field("Directory", root);
            field("Project ID", shared_id);
            c::property(c::tr("Mode").c_str());
            auto modes = overview["modes"];
            auto current = c::tr(modes[mode]["label"].get<std::string>().c_str());
            if (ImGui::BeginCombo("##mode", current.c_str())) {
                for (size_t i = 0; i < modes.size(); ++i)
                    if (ImGui::Selectable(
                            c::tr(modes[i]["label"].get<std::string>().c_str()).c_str(),
                            i == static_cast<size_t>(mode)))
                        mode = static_cast<int>(i);
                ImGui::EndCombo();
            }
            if (modes[mode]["id"] == "git-bundles")
                field("Exchange directory", exchange);
            c::property(c::tr("Interval (minutes)").c_str());
            ImGui::InputInt("##new-interval", &interval);
            c::property(c::tr("Snapshots to keep").c_str());
            ImGui::InputInt("##new-keep", &keep);
            c::property(c::tr("Reuse blocks for large files").c_str());
            c::toggleSwitch("##new-blocks", &reuse_blocks);
            c::endProperties();
        }
        c::textDim("%s", c::tr("Use a separate exchange directory and the same "
                               "project ID on each peer.")
                             .c_str());
        if (button("Create", true)) {
            Json data = {
                {"name", name},
                {"root", root},
                {"mode", overview["modes"][mode]["id"]},
                {"exchange_path", overview["modes"][mode]["id"] == "git-bundles" ? exchange : ""},
                {"backup_interval_minutes", interval},
                {"backup_storage", reuse_blocks ? "auto" : "whole"},
                {"backup_keep", keep}};
            if (!shared_id.empty())
                data["id"] = shared_id;
            submit([this, data] { return core.request("POST", "/api/projects", data); },
                   [this](const Json &r) {
                       selected = r["id"];
                       create = false;
                       refresh();
                   });
        }
        ImGui::SameLine();
        if (button("Cancel"))
            create = false;
        c::endModal();
    }
};
static void capture(const fs::path &path, int width, int height) {
    size_t stride = (static_cast<size_t>(width) * 3 + 3) & ~size_t(3);
    std::vector<unsigned char> pixels(stride * height);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            std::swap(pixels[y * stride + x * 3], pixels[y * stride + x * 3 + 2]);
    std::array<unsigned char, 54> header{};
    auto put = [&](int at, uint32_t value) {
        for (int i = 0; i < 4; ++i)
            header[at + i] = static_cast<unsigned char>((value >> (i * 8)) & 255);
    };
    header[0] = 'B';
    header[1] = 'M';
    put(2, static_cast<uint32_t>(54 + pixels.size()));
    put(10, 54);
    put(14, 40);
    put(18, width);
    put(22, height);
    header[26] = 1;
    header[28] = 24;
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<char *>(header.data()), header.size());
    out.write(reinterpret_cast<char *>(pixels.data()), pixels.size());
    if (!out)
        throw std::runtime_error("Cannot save desktop capture");
}
static int run(std::vector<std::string> args) {
    GLFWwindow *window = nullptr;
    try {
        auto state = cysynck::default_state();
        std::vector<std::string> plugins;
        std::string screenshot, view, theme, language;
        int frames = 0;
        bool first = false, background = false, foreground = false, no_tray = false;
        for (size_t i = 0; i < args.size(); ++i) {
            auto flag = args[i];
            if (flag == "--first-project") {
                first = true;
                continue;
            }
            if (flag == "--background" || flag == "--foreground" || flag == "--no-tray") {
                if (flag == "--background")
                    background = true;
                else if (flag == "--foreground")
                    foreground = true;
                else
                    no_tray = true;
                continue;
            }
            if (++i == args.size())
                throw std::runtime_error("Missing desktop option value");
            if (flag == "--state")
                state = fs::u8path(args[i]);
            else if (flag == "--plugin")
                plugins.push_back(args[i]);
            else if (flag == "--capture")
                screenshot = args[i];
            else if (flag == "--frames")
                frames = std::stoi(args[i]);
            else if (flag == "--view")
                view = args[i];
            else if (flag == "--theme")
                theme = args[i];
            else if (flag == "--language")
                language = args[i];
            else
                throw std::runtime_error("Unknown desktop option: " + flag);
        }
        std::unique_ptr<cysynck::desktop::Session> desktop_session;
        cysynck::Core core(state);
        cysynck::desktop::LaunchSpec launch{
            cysynck::desktop::current_executable(), core.state(), {}};
        for (const auto &plugin : plugins)
            launch.plugins.push_back(fs::u8path(plugin));
        cysynck::desktop::LoginStartup startup(std::move(launch),
                                               cysynck::desktop::user_startup_directory(),
                                               cysynck::desktop::native_startup_backend());
        desktop_session = std::make_unique<cysynck::desktop::Session>(core.state());
        auto &session = *desktop_session;
        if (!session.primary()) {
            if (foreground || !background)
                session.request_show();
            std::cout << "CySynck is already running for this state directory.\n";
            return 0;
        }
        for (auto &p : plugins)
            core.load_plugin(fs::u8path(p));
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
#ifdef __APPLE__
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        const char *glsl = "#version 150";
#else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        const char *glsl = "#version 130";
#endif
        // Create hidden, then show only after the saved preference and actual
        // notification-area availability have been checked. Captures render offscreen.
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(1440, 960, "CySynck", nullptr, nullptr);
        if (!window)
            throw std::runtime_error("Cannot create native OpenGL window");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init(glsl);
        {
            App app(core, startup);
            if (first)
                app.first_project();
            if (!view.empty())
                app.set_view(view);
            if (!theme.empty())
                app.set_theme(theme);
            if (!language.empty())
                app.set_language(language);
            std::unique_ptr<cysynck::desktop::Tray> tray;
            if (!no_tray && screenshot.empty())
                tray = std::make_unique<cysynck::desktop::Tray>(app.tray_labels());
            if (tray) {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
                while (!tray->available() && std::chrono::steady_clock::now() < deadline) {
                    glfwPollEvents();
                    tray->pump();
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            bool available = tray && tray->available();
            app.set_tray_state(available, tray ? tray->reason()
                                               : "Notification area disabled for this launch");
            bool hidden = screenshot.empty() && !foreground &&
                          (background || app.starts_hidden()) && available;
            if (!hidden && screenshot.empty())
                glfwShowWindow(window);
            bool quit = false, backups_requested = false;
            auto show = [&] {
                hidden = false;
                glfwShowWindow(window);
                glfwRestoreWindow(window);
                glfwFocusWindow(window);
            };
            auto hide = [&] {
                if (available) {
                    hidden = true;
                    glfwHideWindow(window);
                }
            };
            std::jthread scheduler([&](std::stop_token stop) {
                int tick = 15;
                while (!stop.stop_requested()) {
                    if (++tick >= 15) {
                        try {
                            core.tick();
                        } catch (const std::exception &e) {
                            std::cerr << e.what() << '\n';
                        }
                        tick = 0;
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            });
            int count = 0;
            while (!quit) {
                glfwPollEvents();
                if (tray) {
                    tray->pump();
                    bool current = tray->available();
                    if (available != current) {
                        available = current;
                        app.set_tray_state(available, tray->reason());
                        if (!available && hidden)
                            show();
                    }
                    cysynck::desktop::TrayAction action;
                    while (tray->next(action)) {
                        using cysynck::desktop::TrayAction;
                        if (action == TrayAction::show)
                            show();
                        else if (action == TrayAction::open_web)
                            app.open_engine_web();
                        else if (action == TrayAction::backup_all)
                            backups_requested = true;
                        else if (action == TrayAction::quit)
                            quit = true;
                    }
                }
                if (session.take_show())
                    show();
                if (glfwWindowShouldClose(window)) {
                    glfwSetWindowShouldClose(window, GLFW_FALSE);
                    if (app.should_hide_on_close())
                        hide();
                    else
                        quit = true;
                }
                app.poll();
                if (backups_requested && !app.busy()) {
                    app.backup_all();
                    backups_requested = false;
                }
                if (app.take_hide())
                    hide();
                if (app.take_quit())
                    quit = true;
                if (tray)
                    tray->update(app.tray_labels());
                if (quit)
                    break;
                if (screenshot.empty() && (hidden || glfwGetWindowAttrib(window, GLFW_ICONIFIED))) {
                    glfwWaitEventsTimeout(0.1);
                    continue;
                }
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                app.draw();
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q))
                    quit = true;
#ifdef __APPLE__
                if (ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiKey_Q))
                    quit = true;
#endif
                ImGui::Render();
                int width, height;
                glfwGetFramebufferSize(window, &width, &height);
                glViewport(0, 0, width, height);
                auto color = c::vec(c::palette().bg0);
                glClearColor(color.x, color.y, color.z, color.w);
                glClear(GL_COLOR_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                if (frames && ++count >= frames) {
                    if (!screenshot.empty())
                        capture(fs::u8path(screenshot), width, height);
                    break;
                }
                glfwSwapBuffers(window);
                if (!frames)
                    std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            scheduler.request_stop();
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        int n = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string value(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, value.data(), n, nullptr, nullptr);
        value.pop_back();
        args.push_back(value);
    }
    LocalFree(argv);
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
