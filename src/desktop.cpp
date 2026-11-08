#include <cysynck/core.hpp>
#include <cyui/Core.h>
#include <cyui/modules/Diff.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif
#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <functional>
#include <future>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <misc/cpp/imgui_stdlib.h>
#include <thread>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include "desktop_support.hpp"
#include "startup.hpp"
#include "help.hpp"
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
        {{"CyRevision exchange", "Échange CyRevision"},
         {"Publishing name", "Nom de publication"},
         {"Create or show public identity", "Créer ou afficher l’identité publique"},
         {"CyRevision project GUID", "GUID du projet CyRevision"},
         {"Legacy Windows signatures", "Signatures Windows historiques"},
         {"Configure CyRevision exchange", "Configurer l’échange CyRevision"},
         {"Load CyRevision settings", "Charger les paramètres CyRevision"},
         {"Public identity JSON", "Identité publique JSON"},
         {"Verified key fingerprint", "Empreinte de clé vérifiée"},
         {"Trust publishing device", "Approuver l’appareil de publication"},
         {"Publishing device GUID", "GUID de l’appareil de publication"},
         {"Revoke publishing device", "Révoquer l’appareil de publication"},
         {"Export to CyRevision", "Exporter vers CyRevision"},
         {"Refresh CyRevision publications", "Actualiser les publications CyRevision"},
         {"Import signed publication", "Importer la publication signée"},
         {"Preview interrupted import recovery", "Prévisualiser la reprise d’import interrompu"},
         {"Apply import recovery", "Appliquer la reprise d’import"},
         {"Verify the key fingerprint through a separate trusted channel. Private signing keys stay in the profile.",
          "Vérifiez l’empreinte de clé par un canal de confiance distinct. Les clés privées restent dans le profil."},
         {"Use legacy signatures for an unmodified Windows CyRevision receiver. Updated receivers accept LF and legacy Windows signatures.",
          "Utilisez les signatures historiques pour un récepteur CyRevision Windows non corrigé. Les récepteurs corrigés acceptent LF et les anciennes signatures Windows."},
         {"LFS cache", "Cache LFS"},
         {"View recovery receipt", "Voir le reçu de récupération"},
         {"Recovery receipt", "Reçu de récupération"},
         {"Verify recovery objects", "Vérifier les objets récupérés"},
         {"Review preserves the incomplete status. Missing objects still need recovery.",
          "Le marquage conserve l’état incomplet. Les objets absents restent à récupérer."},
         {"Select a Git exchange project.", "Sélectionnez un projet d’échange Git."},
         {"Inspect committed branch history. Leave both imported-branch fields empty to use local "
          "HEAD.",
          "Inspectez l’historique commité. Laissez les deux champs de branche importée vides pour "
          "utiliser le HEAD local."},
         {"Imported peer (optional)", "Pair importé (facultatif)"},
         {"Imported branch (optional)", "Branche importée (facultatif)"},
         {"Inspect LFS cache", "Inspecter le cache LFS"},
         {"Verify LFS cache", "Vérifier le cache LFS"},
         {"Presence checks use metadata. Verification reads complete asset bytes and can take "
          "time.",
          "Le contrôle de présence utilise les métadonnées. La vérification lit les assets "
          "complets et peut prendre du temps."},
         {"Committed LFS objects", "Objets LFS commités"},
         {"Objects", "Objets"},
         {"Present", "Présents"},
         {"Missing", "Absents"},
         {"Damaged", "Endommagés"},
         {"Verified", "Vérifiés"},
         {"Revision", "Révision"},
         {"Present, unverified", "Présent, non vérifié"},
         {"Corrupt", "Corrompu"},
         {"Wrong size", "Taille incorrecte"},
         {"Unsafe path", "Chemin non sûr"},
         {"Recover missing LFS objects", "Récupérer les objets LFS absents"},
         {"Only available missing objects are copied. Existing objects, working files and Git "
          "index stay preserved.",
          "Seuls les objets absents disponibles sont copiés. Les objets existants, les fichiers de "
          "travail et l’index Git sont conservés."},
         {"Exchange source peer", "Pair source de l’échange"},
         {"Preview LFS cache recovery", "Prévisualiser la récupération LFS"},
         {"Recover available objects", "Récupérer les objets disponibles"},
         {"Available missing objects", "Objets absents disponibles"},
         {"Unavailable objects", "Objets indisponibles"},
         {"LFS recovery history", "Historique des récupérations LFS"},
         {"Refresh recovery history", "Actualiser les récupérations"},
         {"Installed objects", "Objets installés"},
         {"Complete", "Terminée"},
         {"Incomplete", "Incomplète"},
         {"{total} objects · {missing} missing · {damaged} damaged · {verified} verified",
          "{total} objets · {missing} absents · {damaged} endommagés · {verified} vérifiés"},
         {"{available} available missing objects · {unavailable} unavailable · {bytes} bytes",
          "{available} objets absents disponibles · {unavailable} indisponibles · {bytes} octets"},
         {"Backup scope", "Périmètre de sauvegarde"},
         {"Backup exclusions", "Exclusions de sauvegarde"},
         {"Backup preview", "Aperçu de sauvegarde"},
         {"One glob per line. * and ? match paths or path components. Rules also affect local "
          "change lists.",
          "Un motif par ligne. * et ? correspondent aux chemins ou à leurs composants. Les règles "
          "affectent aussi la liste des modifications locales."},
         {"Excluded folders are pruned. Links and recovery originals stay excluded.",
          "Les dossiers exclus ne sont pas parcourus. Les liens et les originaux de récupération "
          "restent exclus."},
         {"Reload saved rules", "Recharger les règles enregistrées"},
         {"Preview backup scope", "Prévisualiser le périmètre"},
         {"Save reviewed rules", "Enregistrer les règles prévisualisées"},
         {"Back up reviewed scope", "Sauvegarder le périmètre prévisualisé"},
         {"Included files", "Fichiers inclus"},
         {"Skipped entries", "Éléments exclus"},
         {"Included", "Inclus"},
         {"Matched rule", "Règle correspondante"},
         {"Recovery originals", "Originaux de récupération"},
         {"Link", "Lien"},
         {"Special file", "Fichier spécial"},
         {"Metadata preview; file contents are read when backing up. Concurrent edits can change "
          "the result.",
          "Aperçu des métadonnées ; les contenus sont lus pendant la sauvegarde. Des modifications "
          "simultanées peuvent changer le résultat."},
         {"Storage estimates exclude compression, deduplication and temporary space.",
          "Les estimations de stockage ne tiennent pas compte de la compression, de la "
          "déduplication et de l’espace temporaire."},
         {"{count} included files · {bytes} bytes · {skipped} skipped entries",
          "{count} fichiers inclus · {bytes} octets · {skipped} éléments exclus"},
         {"Plugins", "Plugins"},
         {"Refresh plugins", "Actualiser les plugins"},
         {"No plugins loaded", "Aucun plugin chargé"},
         {"Plugin details", "Détails du plugin"},
         {"Native plugins are loaded explicitly when the host starts.",
          "Les plugins natifs sont chargés explicitement au démarrage de l'application hôte."},
         {"Mode options", "Paramètres du mode"},
         {"These settings belong to this project.", "Ces paramètres appartiennent à ce projet."},
         {"Reload mode options", "Recharger les paramètres du mode"},
         {"Save mode options", "Enregistrer les paramètres du mode"},
         {"Use defaults", "Utiliser les valeurs par défaut"},
         {"Mode options saved", "Paramètres du mode enregistrés"},
         {"No configurable options", "Aucun paramètre configurable"},
         {"Load the plugin providing this mode.", "Chargez le plugin qui fournit ce mode."},
         {"Range", "Plage"},
         {"Maximum UTF-8 bytes", "Nombre maximal d'octets UTF-8"},
         {"Browse saved files", "Parcourir les fichiers sauvegardés"},
         {"Folder prefix", "Dossier à parcourir"},
         {"Metadata only; file contents are verified when previewed or restored.",
          "Métadonnées uniquement ; les contenus sont vérifiés lors d'un aperçu ou d'une "
          "restauration."},
         {"Add folder to selection", "Ajouter le dossier à la sélection"},
         {"Restore selection", "Sélection à restaurer"},
         {"One relative path per line; folders end with /. Leave empty for the whole snapshot.",
          "Un chemin relatif par ligne ; terminez les dossiers par /. Laissez vide pour restaurer "
          "toute la sauvegarde."},
         {"Clear selection", "Vider la sélection"},
         {"Compare saved versions", "Comparer deux sauvegardes"},
         {"Older snapshot", "Sauvegarde de départ"},
         {"Newer snapshot", "Sauvegarde d'arrivée"},
         {"Compare snapshots", "Comparer les sauvegardes"},
         {"Select a file to inspect its saved versions.",
          "Sélectionnez un fichier pour examiner ses versions sauvegardées."},
         {"Preview verifies the selected files before creating a new destination.",
          "L'aperçu vérifie les fichiers sélectionnés avant de créer un nouveau dossier de "
          "sortie."},
         {"Permissions changed", "Permissions modifiées"},
         {"Added", "Ajouté"},
         {"Deleted", "Supprimé"},
         {"Modified", "Modifié"},
         {"Merge diverged branch", "Fusionner les branches divergentes"},
         {"Preview repository merge", "Prévisualiser la fusion du dépôt"},
         {"Apply reviewed merge", "Appliquer la fusion vérifiée"},
         {"Creates a reviewed two-parent merge commit. Choose committed files or edit conflict "
          "text, then preview again. Unrelated local data is preserved.",
          "Crée un commit de fusion vérifié à deux parents. Choisissez des fichiers commités ou "
          "modifiez le texte en conflit, puis prévisualisez à nouveau. Les données locales sans "
          "collision sont conservées."},
         {"Edit merged text", "Modifier le texte fusionné"},
         {"Local LFS assets", "Assets LFS locaux"},
         {"LFS materialization receipts", "Reçus de matérialisation LFS"},
         {"LFS materialization incomplete", "Matérialisation LFS interrompue"},
         {"Index metadata refreshed; staged entries and flags match",
          "Métadonnées de l'index actualisées ; les entrées indexées et les drapeaux "
          "correspondent"},
         {"Original pointer", "Pointeur d'origine"},
         {"Verified asset", "Asset vérifié"},
         {"Publication interrupted", "Publication interrompue"},
         {"Local contents differ", "Le contenu local diffère"},
         {"Load LFS receipts", "Charger les reçus LFS"},
         {"Check LFS receipt", "Vérifier le reçu LFS"},
         {"No LFS receipts", "Aucun reçu LFS"},
         {"LFS files match the original pointers",
          "Les fichiers LFS correspondent aux pointeurs d'origine"},
         {"LFS assets and recovery pointers verified",
          "Assets LFS et pointeurs de récupération vérifiés"},
         {"LFS recovery needs manual inspection",
          "La récupération LFS nécessite une inspection manuelle"},
         {"Receipt checks preserve working files, staged assets and original pointers. Mark "
          "reviewed only after an exact before or after check.",
          "La vérification conserve les fichiers de travail, les assets préparés et les pointeurs "
          "d'origine. Marquez comme examiné uniquement après une vérification exacte avant ou "
          "après."},
         {"Preview LFS materialization", "Prévisualiser la matérialisation LFS"},
         {"Preview LFS resume", "Prévisualiser la reprise LFS"},
         {"LFS asset export directory", "Dossier d'export des assets LFS"},
         {"Choose a new absolute directory outside all projects and the profile.",
          "Choisissez un nouveau dossier absolu hors des projets et du profil."},
         {"Preview preserved asset export", "Prévisualiser l'export des assets conservés"},
         {"Export verified preserved assets", "Exporter les assets conservés vérifiés"},
         {"Preserved asset export complete", "Export des assets conservés terminé"},
         {"Assets to export", "Assets à exporter"},
         {"Resume verified publication", "Reprendre la publication vérifiée"},
         {"LFS recovery can be resumed", "La récupération LFS peut être reprise"},
         {"Already published assets", "Assets déjà publiés"},
         {"Pending publications", "Publications en attente"},
         {"Assets to stage from cache", "Assets à préparer depuis le cache"},
         {"Staging bytes required", "Octets à préparer"},
         {"Pointers to preserve", "Pointeurs à préserver"},
         {"Prepare pointers for Git updates", "Préparer les pointeurs pour les mises à jour Git"},
         {"Preserve unchanged expanded assets in recovery storage and restore their committed "
          "pointers before Git integration.",
          "Conserver les assets inchangés dans le dossier de récupération et rétablir leurs "
          "pointeurs avant l’intégration Git."},
         {"Preview LFS pointer checkout", "Prévisualiser le retour aux pointeurs LFS"},
         {"Restore committed LFS pointers", "Rétablir les pointeurs LFS du commit"},
         {"Already committed pointers", "Pointeurs déjà en place"},
         {"Assets to preserve", "Assets à conserver"},
         {"Asset to preserve", "Asset à conserver"},
         {"LFS pointer checkout complete", "Retour aux pointeurs LFS terminé"},
         {"LFS pointer checkout incomplete", "Retour aux pointeurs LFS interrompu"},
         {"LFS pointers and preserved assets verified",
          "Pointeurs LFS et assets conservés vérifiés"},
         {"Original LFS assets verified", "Assets LFS d’origine vérifiés"},
         {"LFS publication resumed", "Publication LFS reprise"},
         {"LFS publication resume incomplete", "Reprise de publication LFS interrompue"},
         {"Materialize verified assets", "Matérialiser les assets vérifiés"},
         {"Replace exact committed pointers with verified cached assets. Modified, missing and "
          "staged files are preserved.",
          "Remplace les pointeurs commités exacts par les assets vérifiés du cache. Les fichiers "
          "modifiés, absents et indexés sont conservés."},
         {"Pending assets", "Assets en attente"},
         {"LFS selection", "Sélection LFS"},
         {"One relative path per line; folders end with /. Leave empty for all LFS pointers.",
          "Un chemin relatif par ligne ; les dossiers se terminent par /. Laissez vide pour tous "
          "les pointeurs LFS."},
         {"Excluded LFS pointers", "Pointeurs LFS hors sélection"},
         {"Already materialized", "Déjà matérialisés"},
         {"Preserved files", "Fichiers conservés"},
         {"Asset bytes", "Taille des assets en octets"},
         {"LFS materialization complete", "Matérialisation LFS terminée"},
         {"Recovery directory", "Dossier de récupération"},
         {"LFS file states", "État des fichiers LFS"},
         {"Committed pointer", "Pointeur commité"},
         {"Local modifications preserved", "Modifications locales conservées"},
         {"Local deletion preserved", "Suppression locale conservée"},
         {"Staged changes preserved", "Modifications indexées conservées"},
         {"Special index flags preserved", "Drapeaux d'index particuliers conservés"},
         {"Read-only file preserved", "Fichier en lecture seule conservé"},
         {"LFS attributes required", "Attributs LFS requis"},
         {"Materialization blocked", "Matérialisation bloquée"},
         {"LFS cache object unavailable", "Objet du cache LFS indisponible"},
         {"Choose conflict regions", "Choisir les blocs de conflit"},
         {"Conflict region", "Bloc de conflit"},
         {"Use local region", "Utiliser le bloc local"},
         {"Use peer region", "Utiliser le bloc du pair"},
         {"Use both (local then peer)", "Utiliser les deux (local puis pair)"},
         {"Build draft from selected regions", "Créer le brouillon à partir des blocs choisis"},
         {"Building a draft replaces the editor text. Review it before use.",
          "Créer un brouillon remplace le texte de l'éditeur. Vérifiez-le avant utilisation."},
         {"Region choices are unavailable for this proposal; edit the text manually.",
          "Les choix par bloc sont indisponibles pour cette proposition ; modifiez le texte "
          "manuellement."},
         {"Use edited text", "Utiliser le texte modifié"},
         {"Cancel edit", "Annuler la modification"},
         {"Edited text selected", "Texte modifié sélectionné"},
         {"Edited text bytes", "Taille du texte modifié en octets"},
         {"Edited files", "Fichiers modifiés manuellement"},
         {"Text editing requires matching file permissions",
          "La modification du texte nécessite des permissions de fichier identiques"},
         {"Remove conflict markers and review the final text. Maximum 512 KiB; edits are saved in "
          "the private receipt.",
          "Retirez les marqueurs de conflit et vérifiez le texte final. Maximum 512 Kio ; les "
          "modifications sont enregistrées dans le reçu privé."},
         {"Remove conflict marker lines from edited Git text before previewing",
          "Retirez les lignes de marqueurs de conflit du texte Git modifié avant la "
          "prévisualisation"},
         {"Each choice replaces the entire file, including nonconflicting changes. A missing "
          "version deletes it.",
          "Chaque choix remplace le fichier entier, y compris les changements sans conflit. Une "
          "version absente le supprime."},
         {"Use local file", "Utiliser le fichier local"},
         {"Use peer file", "Utiliser le fichier du pair"},
         {"Use local deletion", "Utiliser la suppression locale"},
         {"Use peer deletion", "Utiliser la suppression du pair"},
         {"Leave unresolved", "Laisser non résolu"},
         {"Resolved conflicts", "Conflits résolus"},
         {"Choices changed; preview again", "Choix modifiés : prévisualisez à nouveau"},
         {"Choose a committed version for every conflict, then preview again",
          "Choisissez une version commitée pour chaque conflit, puis prévisualisez à nouveau"},
         {"Reviewed result tree", "Arbre résultant vérifié"},
         {"Merge author", "Auteur de la fusion"},
         {"Repository merge conflicts", "Conflits de fusion du dépôt"},
         {"Merge commit", "Commit de fusion"},
         {"Three-way Git review", "Revue Git à trois versions"},
         {"Compares committed files with their common ancestor. Proposals do not apply a "
          "repository merge or include working changes and rename detection.",
          "Compare les fichiers commités à leur ancêtre commun. Les propositions n’appliquent "
          "aucune fusion au dépôt et n’incluent ni les changements de travail ni la détection des "
          "renommages."},
         {"Review three-way changes", "Examiner les trois versions"},
         {"Three-way review needs a local commit",
          "La revue à trois versions nécessite un commit local"},
         {"Three-way review needs a shared ancestor",
          "La revue à trois versions nécessite un ancêtre commun"},
         {"Multiple merge bases require a repository merge",
          "Plusieurs ancêtres de fusion nécessitent une fusion dans Git"},
         {"Common ancestor", "Ancêtre commun"},
         {"Local change only", "Changement local uniquement"},
         {"Peer change only", "Changement du pair uniquement"},
         {"Same change on both sides", "Même changement des deux côtés"},
         {"Changed on both sides", "Modifié des deux côtés"},
         {"Structural conflict", "Conflit de structure"},
         {"Preview three-way file", "Prévisualiser les trois versions"},
         {"Text proposal without conflicts", "Proposition de texte sans conflit"},
         {"Text conflicts detected", "Conflits de texte détectés"},
         {"Proposed file deletion", "Suppression du fichier proposée"},
         {"Manual Git review required", "Revue Git manuelle nécessaire"},
         {"Review file types and path collisions in Git",
          "Examinez les types de fichiers et les collisions de chemins dans Git"},
         {"Review submodules in Git", "Examinez les sous-modules dans Git"},
         {"Review symbolic links in Git", "Examinez les liens symboliques dans Git"},
         {"Three-way text previews are limited to 512 KiB per side",
          "La prévisualisation à trois versions est limitée à 512 Kio par version"},
         {"Binary or non-UTF-8 content needs manual review",
          "Les contenus binaires ou non UTF-8 nécessitent une revue manuelle"},
         {"LFS assets need manual Git review", "Les assets LFS nécessitent une revue Git manuelle"},
         {"Custom merge or checkout attributes need manual Git review",
          "Les attributs de fusion ou d’extraction personnalisés nécessitent une revue Git "
          "manuelle"},
         {"One side modified a file deleted by the other",
          "Un côté a modifié un fichier supprimé par l’autre"},
         {"File permissions need manual Git review",
          "Les permissions du fichier nécessitent une revue Git manuelle"},
         {"Merge proposal exceeds its preview bound",
          "La proposition de fusion dépasse la limite de prévisualisation"},
         {"Conflict regions reported", "Zones de conflit signalées"},
         {"Merge proposal", "Proposition de fusion"},
         {"Local committed version", "Version locale commitée"},
         {"Peer committed version", "Version commitée du pair"},
         {"Backup storage", "Stockage des sauvegardes"},
         {"Fixed blocks (4 MiB)", "Blocs fixes (4 Mio)"},
         {"Whole files", "Fichiers entiers"},
         {"Content-defined blocks (1–8 MiB)", "Blocs selon le contenu (1–8 Mio)"},
         {"Content-defined blocks support files from 8 MiB to 64 GiB and can preserve reuse after "
          "insertions. Other sizes use whole files.",
          "Les blocs selon le contenu couvrent les fichiers de 8 Mio à 64 Gio et peuvent être "
          "réutilisés après des insertions. Les autres tailles sont stockées en fichiers entiers."},
         {"Content-defined file versions", "Versions de fichiers découpées selon le contenu"},
         {"Content-defined file versions: {count}",
          "Versions de fichiers découpées selon le contenu : {count}"},
         {"Git integration receipts", "Reçus d’intégration Git"},
         {"Check the repository before marking an interruption as reviewed. Receipts and Git files "
          "are preserved.",
          "Vérifiez le dépôt avant de marquer une interruption comme examinée. Les reçus et les "
          "fichiers Git sont conservés."},
         {"Load integration receipts", "Charger les reçus d’intégration"},
         {"No integration receipts", "Aucun reçu d’intégration"},
         {"Integration incomplete", "Intégration incomplète"},
         {"Recovery reviewed", "Récupération examinée"},
         {"Check integration receipt", "Vérifier le reçu d’intégration"},
         {"Repository at integrated commit", "Dépôt au commit intégré"},
         {"Repository at original commit", "Dépôt au commit d’origine"},
         {"Repository state differs or is unavailable",
          "L’état du dépôt diffère ou est indisponible"},
         {"Tracked files and index verified", "Fichiers suivis et index vérifiés"},
         {"Local data verification unavailable", "Vérification des données locales indisponible"},
         {"Local data metadata verified", "Métadonnées des données locales vérifiées"},
         {"Receipt scan limit reached; inspect the state directory for older records.",
          "Limite de lecture des reçus atteinte ; consultez le dossier d’état pour les autres "
          "reçus."},
         {"Previous receipts", "Reçus précédents"},
         {"Next receipts", "Reçus suivants"},
         {"Inspect the repository and confirm no integration is running before manually removing "
          "its leftover integration lock",
          "Inspectez le dépôt et confirmez qu’aucune intégration n’est active avant de retirer "
          "manuellement le verrou restant"},
         {"The current Git branch or commit differs from both receipt states",
          "La branche ou le commit Git actuel diffère des deux états du reçu"},
         {"Receipt file listings do not match their Git commit objects",
          "Les listes de fichiers du reçu ne correspondent pas à leurs objets Git"},
         {"The Git index differs from the receipt commit; preserve staged work",
          "L’index Git diffère du commit du reçu ; préservez les changements indexés"},
         {"Clear assume-unchanged and skip-worktree flags before verification",
          "Retirez les indicateurs assume-unchanged et skip-worktree avant la vérification"},
         {"Tracked working files differ from the receipt commit",
          "Les fichiers suivis diffèrent du commit du reçu"},
         {"Tracked executable permissions differ from the receipt commit",
          "Les permissions exécutables diffèrent du commit du reçu"},
         {"Tracked working files or index changed during verification",
          "Les fichiers suivis ou l’index ont changé pendant la vérification"},
         {"Local data metadata differs from the integration receipt",
          "Les métadonnées locales diffèrent du reçu d’intégration"},
         {"Git HEAD changed during receipt verification; check again",
          "HEAD a changé pendant la vérification ; vérifiez à nouveau"},
         {"Direct transfers", "Transferts directs"},
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
         {"Repository setup", "Initialisation du dépôt"},
         {"Extract Git revision", "Extraire la révision Git"},
         {"The imported branch is already applied", "La branche importée est déjà appliquée"},
         {"Select a local branch before integrating; detached HEAD is preserved",
          "Sélectionnez une branche locale avant l'intégration ; le HEAD détaché est préservé"},
         {"Integration requires a fast-forward; divergent or older branches need manual Git merge",
          "L'intégration exige une avance rapide ; les branches divergentes ou plus anciennes "
          "nécessitent une fusion Git manuelle"},
         {"Commit or preserve staged changes before integration",
          "Commitez ou préservez les changements de l'index avant l'intégration"},
         {"Preserve staged files before the first integration",
          "Préservez les fichiers de l'index avant la première intégration"},
         {"Clear assume-unchanged and skip-worktree index flags before integration",
          "Retirez les indicateurs assume-unchanged et skip-worktree avant l'intégration"},
         {"Preserve modified or missing working files before integration",
          "Préservez les fichiers de travail modifiés ou manquants avant l'intégration"},
         {"Move untracked and ignored files outside the project before integration",
          "Déplacez les fichiers non suivis et ignorés hors du projet avant l'intégration"},
         {"Tracked files and index must be clean before integration",
          "Les fichiers suivis et l'index doivent être propres avant l'intégration"},
         {"LFS revisions require separate extraction; Git integration does not hydrate assets yet",
          "Les révisions LFS nécessitent une extraction séparée ; l'intégration Git ne restaure "
          "pas encore les assets"},
         {"Git integration excludes checkout filters, encodings and text conversions; use separate "
          "extraction",
          "Les filtres, encodages et conversions de texte nécessitent une extraction séparée"},
         {"Git integration excludes linked files and submodules",
          "Les liens et sous-modules nécessitent une intégration Git manuelle"},
         {"Integrate imported branch", "Intégrer la branche importée"},
         {"Preview integration", "Prévisualiser l'intégration"},
         {"Keep LFS pointers during fast-forward",
          "Conserver les pointeurs LFS pendant l’avance rapide"},
         {"LFS assets are not expanded. Materialize them separately after integration. Modified or "
          "expanded working files block this operation.",
          "Les assets LFS ne sont pas développés. Matérialisez-les séparément après l’intégration. "
          "Les fichiers modifiés ou déjà développés bloquent cette opération."},
         {"LFS pointer files", "Fichiers pointeurs LFS"},
         {"Updates the local branch, index and tracked files. Unrelated local files are preserved. "
          "LFS pointers require explicit selection.",
          "Met à jour la branche locale, l’index et les fichiers suivis. Les fichiers locaux "
          "indépendants sont conservés. Les pointeurs LFS exigent une sélection explicite."},
         {"Apply fast-forward", "Appliquer l'avance rapide"},
         {"Local files preserved", "Fichiers locaux conservés"},
         {"Integration complete", "Intégration terminée"},
         {"Integration blocked", "Intégration bloquée"},
         {"Updates the local branch, index and tracked files. Unrelated local files are preserved; "
          "LFS and checkout filters use separate extraction.",
          "Met à jour la branche locale, l'index et les fichiers suivis. Les fichiers locaux sans "
          "collision sont conservés ; LFS et les filtres nécessitent une extraction séparée."},
         {"Preview extraction", "Prévisualiser l'extraction"},
         {"Extract to new directory", "Extraire vers un nouveau dossier"},
         {"New extraction directory", "Nouveau dossier d'extraction"},
         {"Expand verified LFS assets", "Extraire les assets LFS vérifiés"},
         {"Copies the complete selected revision. Local Git files and changes are preserved.",
          "Copie toute la révision sélectionnée. Les fichiers Git locaux et les modifications sont "
          "préservés."},
         {"Extraction complete", "Extraction terminée"},
         {"Extraction blocked", "Extraction bloquée"},
         {"File snapshot limits: 1 GiB per Git blob, 4 GiB per LFS asset, 16 GiB total.",
          "Limites de la copie : 1 Gio par blob Git, 4 Gio par asset LFS, 16 Gio au total."},
         {"Initial branch", "Branche initiale"},
         {"Git object format", "Format des objets Git"},
         {"Preview Git setup", "Prévisualiser l'initialisation Git"},
         {"Initialize repository", "Initialiser le dépôt"},
         {"Initialization creates Git metadata and preserves existing files. It does not check out "
          "peer files.",
          "L'initialisation crée les données Git et préserve les fichiers existants. Elle "
          "n'extrait pas les fichiers des pairs."},
         {"No local commit yet", "Aucun commit local pour le moment"},
         {"Branch history", "Historique de la branche"},
         {"Load branch history", "Charger l'historique de la branche"},
         {"Previous page", "Page précédente"},
         {"Next page", "Page suivante"},
         {"Review commit", "Revoir le commit"},
         {"Compare with parent", "Comparer au parent"},
         {"Return to branch comparison", "Revenir à la comparaison des branches"},
         {"Root commit", "Commit initial"},
         {"Author", "Auteur"},
         {"Committer", "Validateur"},
         {"No changed files", "Aucun fichier modifié"},
         {"History page limit reached", "Limite des pages d'historique atteinte"},
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
    std::future<Json> help_pending;
    std::function<void(const Json &)> callback;
    Json overview, events,
        snapshots = Json::array(), changed = Json::array(), diff, git_state, network_state,
        incoming = Json::array(), messages = Json::array(), engine_state, engine_devices,
        startup_state, backup_check, restore_preview, storage_state, conflict_state, conflict_diff,
        resolution_preview, resolution_preview_input, resolution_history, received_state,
        doctor_state, service_preview, service_preview_input, transfer_state, transfer_limits_state,
        git_branches, git_review_state, git_review_diff, git_storage_state, git_history_state,
        git_commit_state, git_setup_state, git_extract_state, git_extract_input,
        git_integrate_state, git_integrate_input, git_integration_records, git_integration_check,
        git_merge_state, git_merge_file_state, git_merge_editor, git_materialize_state,
        git_materialization_records, git_materialization_check, git_materialization_resume,
        git_asset_export_state;
    Json lfs_cache_state, lfs_cache_plan, lfs_cache_records;
    Json lfs_recovery_receipt, lfs_recovery_check;
    std::string lfs_revision_peer, lfs_revision_branch, lfs_source_peer;
    bool lfs_cache_requested = false, lfs_cache_verify = false;
    size_t lfs_cache_revision = 0;
    Json backup_files, snapshot_comparison, snapshot_file_diff;
    Json backup_scope_state;
    std::string backup_rules_text;
    bool backup_rules_loaded = false;
    uint64_t backup_scope_revision = 0;
    Json plugins_state, mode_options_state;
    bool plugins_requested = false, mode_options_requested = false, mode_options_saved = false;
    size_t mode_options_revision = 0;
    std::string backup_prefix, restore_paths, history_left, history_right;
    size_t git_integration_offset = 0;
    size_t git_materialization_offset = 0;
    std::string git_materialization_paths, git_asset_export_destination;
    bool git_dehydrate = false;
    std::string git_extract_destination;
    bool git_extract_lfs = true;
    std::string git_initial_branch = "main";
    int git_object_format = 0;
    std::set<std::string> git_storage_selected;
    std::map<std::string, std::string> git_merge_choices;
    bool git_integrate_lfs_pointers = false;
    std::map<std::string, std::string> git_merge_edits;
    std::string git_merge_buffer;
    std::vector<std::string> git_region_choices;
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
    bool git_loaded = false, git_setup_requested = false;
    Json cyrevision_state, cyrevision_incoming, cyrevision_recovery;
    std::string cyrevision_name, cyrevision_guid, cyrevision_public_json, cyrevision_fingerprint, cyrevision_device;
    bool cyrevision_legacy_windows = false;
    int backup_policy = 0;
    static const char *backup_policy_value(int index) {
        return index == 1 ? "whole" : index == 2 ? "cdc" : "auto";
    }
    static int backup_policy_index(const Json &p) {
        auto value = p.value("backup_storage", "auto");
        return value == "whole" ? 1 : value == "cdc" ? 2 : 0;
    }
    void backup_policy_selector(const char *id) {
        const char *labels[] = {"Fixed blocks (4 MiB)", "Whole files",
                                "Content-defined blocks (1–8 MiB)"};
        if (ImGui::BeginCombo(id, c::tr(labels[backup_policy]).c_str())) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(c::tr(labels[i]).c_str(), backup_policy == i))
                    backup_policy = i;
            ImGui::EndCombo();
        }
    }
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
    c::DiffDocument backup_document;
    c::DiffState backup_diff_view;
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
            backup_policy = backup_policy_index(p);
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
        cyrevision_state = cyrevision_incoming = cyrevision_recovery = nullptr;
        cyrevision_guid.clear(); cyrevision_public_json.clear(); cyrevision_fingerprint.clear(); cyrevision_device.clear();
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
        lfs_cache_state = lfs_cache_plan = lfs_cache_records = nullptr;
        lfs_recovery_receipt = lfs_recovery_check = nullptr;
        lfs_revision_peer.clear();
        lfs_revision_branch.clear();
        lfs_source_peer.clear();
        lfs_cache_requested = lfs_cache_verify = false;
        ++lfs_cache_revision;
        backup_scope_state = nullptr;
        backup_rules_text.clear();
        backup_rules_loaded = false;
        ++backup_scope_revision;
        mode_options_state = nullptr;
        mode_options_requested = mode_options_saved = false;
        ++mode_options_revision;
        backup_files = snapshot_comparison = snapshot_file_diff = nullptr;
        backup_prefix.clear();
        restore_paths.clear();
        history_left.clear();
        history_right.clear();
        git_state = nullptr;
        git_branches = git_review_state = git_review_diff = git_history_state = git_commit_state =
            nullptr;
        git_setup_state = nullptr;
        git_extract_state = git_extract_input = nullptr;
        git_integrate_state = git_integrate_input = nullptr;
        git_integrate_lfs_pointers = false;
        clear_git_merge_choices();
        git_merge_state = git_merge_file_state = nullptr;
        git_integration_records = git_integration_check = nullptr;
        git_materialize_state = nullptr;
        git_materialization_paths.clear();
        git_dehydrate = false;
        git_materialization_records = git_materialization_check = nullptr;
        git_materialization_resume = nullptr;
        git_asset_export_state = nullptr;
        git_asset_export_destination.clear();
        git_materialization_offset = 0;
        git_integration_offset = 0;
        git_extract_destination.clear();
        git_extract_lfs = true;
        git_setup_requested = false;
        git_initial_branch = "main";
        git_object_format = 0;
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
        backup_policy = backup_policy_index(p);
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
        if (help_pending.valid() &&
            help_pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                (void)help_pending.get();
            } catch (const std::exception &e) {
                error = e.what();
                c::notify(error.c_str(), c::Tone::error, 8);
            }
        }
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
        if (ImGui::Button(language == "fr" ? "Aide (F1)" : "Help (F1)"))
            open_help();
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
        for (auto page : {"Changes", "Conflicts", "Backups", "Backup scope", "Git exchange",
                          "LFS cache", "Engine", "Network", "Activity", "Storage", "Diagnostics",
                          "Services", "Plugins", "Command line", "Settings", "Application"})
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
        c::textDim("%s", (view == "Storage" || view == "Diagnostics" || view == "Services" ||
                          view == "Plugins")
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
        else if (view == "Plugins")
            plugins_view();
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
        else if (view == "LFS cache")
            lfs_cache_view(p);
        else if (view == "Backup scope")
            backup_scope_view(p);
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
    std::string plugin_label(const Json &value, const char *key) const {
        auto french = std::string(key) + "_fr";
        if (language == "fr" && value.contains(french) &&
            !value.at(french).get<std::string>().empty())
            return value.at(french).get<std::string>();
        return value.value(key, std::string{});
    }
    void plugins_view() {
        ImGui::TextWrapped(
            "%s", c::tr("Native plugins are loaded explicitly when the host starts.").c_str());
        ImGui::BeginDisabled(busy());
        bool reload = button("Refresh plugins");
        ImGui::EndDisabled();
        if ((reload || !plugins_requested) && !busy()) {
            plugins_requested = true;
            submit([this] { return core.request("GET", "/api/plugins"); },
                   [this](const Json &r) { plugins_state = r; });
        }
        if (plugins_state.is_null())
            return;
        if (plugins_state.at("plugins").empty())
            text(c::tr("No plugins loaded"));
        for (const auto &plugin : plugins_state.at("plugins")) {
            auto id = plugin.at("id").get<std::string>();
            auto title = plugin_label(plugin, "name");
            if (!plugin.at("version").get<std::string>().empty())
                title += " / " + plugin.at("version").get<std::string>();
            c::beginCard(id.c_str(), title.c_str());
            ImGui::TextWrapped("%s", plugin_label(plugin, "description").c_str());
            for (const auto &mode : plugin.at("modes")) {
                text(plugin_label(mode, "label"));
                ImGui::TextWrapped("%s", plugin_label(mode, "description").c_str());
            }
            if (ImGui::CollapsingHeader(c::trLabel("Plugin details").c_str())) {
                text(id + " / ABI " + plugin.at("abi_version").dump());
                ImGui::TextWrapped("%s", plugin.at("path").get<std::string>().c_str());
                text("SHA-256: " + plugin.at("sha256").get<std::string>());
            }
            c::endCard();
        }
    }
    Json lfs_cache_input() const {
        Json data = Json::object();
        if (!lfs_revision_peer.empty() || !lfs_revision_branch.empty())
            data.update({{"peer", lfs_revision_peer}, {"branch", lfs_revision_branch}});
        return data;
    }
    void load_lfs_cache(bool verify, size_t offset = 0, bool continuation = false) {
        auto id = selected;
        auto revision = ++lfs_cache_revision;
        auto input = lfs_cache_input();
        input.update({{"verify", verify}, {"offset", offset}, {"limit", 100}});
        if (continuation && !lfs_cache_state.is_null())
            input["inventory_hash"] = lfs_cache_state.at("inventory_hash");
        lfs_cache_requested = true;
        lfs_cache_verify = verify;
        lfs_cache_plan = nullptr;
        submit(
            [this, id, input] {
                return core.request("POST", "/api/projects/" + id + "/git-lfs-cache", input);
            },
            [this, id, revision](const Json &r) {
                if (selected == id && lfs_cache_revision == revision) {
                    lfs_cache_state = r;
                    if (lfs_source_peer.empty() && !r.at("source_peers").empty())
                        lfs_source_peer = r.at("source_peers").at(0);
                }
            });
    }
    void load_lfs_cache_history(size_t offset = 0) {
        action("git-lfs-recoveries", {{"offset", offset}, {"limit", 20}},
               [this](const Json &r) { lfs_cache_records = r; });
    }
    void lfs_cache_view(const Json &p) {
        if (p.at("mode") != "git-bundles") {
            text(c::tr("Select a Git exchange project."));
            return;
        }
        c::beginCard("lfs-cache-context", c::tr("LFS cache").c_str());
        ImGui::TextWrapped("%s", c::tr("Inspect committed branch history. Leave both "
                                       "imported-branch fields empty to use local HEAD.")
                                     .c_str());
        ImGui::BeginDisabled(busy());
        text(c::tr("Imported peer (optional)"));
        bool edited = ImGui::InputText("##lfs-peer", &lfs_revision_peer);
        text(c::tr("Imported branch (optional)"));
        edited |= ImGui::InputText("##lfs-branch", &lfs_revision_branch);
        if (edited) {
            ++lfs_cache_revision;
            lfs_cache_state = lfs_cache_plan = nullptr;
        }
        bool inspect = button("Inspect LFS cache");
        ImGui::SameLine();
        bool verify = button("Verify LFS cache", true);
        ImGui::EndDisabled();
        if ((inspect || verify || !lfs_cache_requested) && !busy())
            load_lfs_cache(verify);
        ImGui::TextWrapped("%s", c::tr("Presence checks use metadata. Verification reads complete "
                                       "asset bytes and can take time.")
                                     .c_str());
        c::endCard();
        if (lfs_cache_state.is_null())
            return;
        const auto &cache = lfs_cache_state;
        c::space(12);
        c::beginCard("lfs-cache-inventory", c::tr("Committed LFS objects").c_str());
        for (auto key : {"total", "present", "missing", "damaged", "verified"}) {
            auto label = key == std::string("total")     ? "Objects"
                         : key == std::string("present") ? "Present"
                         : key == std::string("missing") ? "Missing"
                         : key == std::string("damaged") ? "Damaged"
                                                         : "Verified";
            c::textDim("%s: %zu", c::tr(label).c_str(), cache.at(key).get<size_t>());
        }
        ImGui::TextWrapped("%s: %s", c::tr("Revision").c_str(),
                           cache.at("revision").get<std::string>().c_str());
        auto offset = cache.at("offset").get<size_t>();
        ImGui::BeginDisabled(busy() || !offset);
        if (button("Previous page"))
            load_lfs_cache(lfs_cache_verify, offset >= 100 ? offset - 100 : 0, true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(busy() || !cache.at("has_more").get<bool>());
        if (button("Next page"))
            load_lfs_cache(lfs_cache_verify, offset + 100, true);
        ImGui::EndDisabled();
        ImGui::BeginChild("cache-objects", ImVec2(0, 155));
        for (const auto &object : cache.at("objects")) {
            auto state = object.at("state").get<std::string>();
            auto label = state == "missing"         ? "Missing"
                         : state == "present"       ? "Present, unverified"
                         : state == "verified"      ? "Verified"
                         : state == "corrupt"       ? "Corrupt"
                         : state == "size_mismatch" ? "Wrong size"
                                                    : "Unsafe path";
            ImGui::TextWrapped("%s / %s / %llu B", object.at("sha256").get<std::string>().c_str(),
                               c::tr(label).c_str(),
                               static_cast<unsigned long long>(object.at("size").get<uintmax_t>()));
        }
        ImGui::EndChild();
        c::endCard();
        c::space(12);
        c::beginCard("lfs-cache-refill", c::tr("Recover missing LFS objects").c_str());
        ImGui::TextWrapped("%s", c::tr("Only available missing objects are copied. Existing "
                                       "objects, working files and Git index stay preserved.")
                                     .c_str());
        ImGui::BeginDisabled(busy());
        text(c::tr("Exchange source peer"));
        if (ImGui::BeginCombo("##source-peer", lfs_source_peer.c_str())) {
            for (const auto &peer : cache.at("source_peers")) {
                auto value = peer.get<std::string>();
                if (ImGui::Selectable(value.c_str(), value == lfs_source_peer)) {
                    lfs_source_peer = value;
                    lfs_cache_plan = nullptr;
                    ++lfs_cache_revision;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(lfs_source_peer.empty());
        if (button("Preview LFS cache recovery")) {
            auto input = lfs_cache_input();
            input["source_peer"] = lfs_source_peer;
            auto revision = ++lfs_cache_revision;
            lfs_cache_plan = nullptr;
            action("git-lfs-recover", input, [this, revision](const Json &r) {
                if (revision == lfs_cache_revision)
                    lfs_cache_plan = r;
            });
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!lfs_cache_plan.is_null()) {
            c::textDim(
                "%s: %zu / %s: %zu / %llu B", c::tr("Available missing objects").c_str(),
                lfs_cache_plan.at("objects").size(), c::tr("Unavailable objects").c_str(),
                lfs_cache_plan.at("unavailable").size(),
                static_cast<unsigned long long>(lfs_cache_plan.at("bytes").get<uintmax_t>()));
            ImGui::BeginDisabled(busy() || !lfs_cache_plan.at("can_recover").get<bool>());
            if (button("Recover available objects", true)) {
                auto input = lfs_cache_input();
                input.update({{"source_peer", lfs_source_peer},
                              {"apply", true},
                              {"plan", lfs_cache_plan.at("plan")}});
                action("git-lfs-recover", input, [this](const Json &) {
                    lfs_cache_plan = lfs_cache_state = nullptr;
                    lfs_cache_requested = false;
                    lfs_cache_records = nullptr;
                    lfs_recovery_receipt = lfs_recovery_check = nullptr;
                });
            }
            ImGui::EndDisabled();
        }
        c::endCard();
        c::space(12);
        c::beginCard("lfs-cache-history", c::tr("LFS recovery history").c_str());
        ImGui::PushID("lfs-history");
        ImGui::BeginDisabled(busy());
        if (button("Refresh recovery history"))
            load_lfs_cache_history();
        ImGui::EndDisabled();
        if (!lfs_cache_records.is_null()) {
            auto offset = lfs_cache_records.at("offset").get<size_t>();
            ImGui::BeginDisabled(busy() || !offset);
            if (button("Previous page"))
                load_lfs_cache_history(offset >= 20 ? offset - 20 : 0);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(busy() || !lfs_cache_records.at("has_more").get<bool>());
            if (button("Next page"))
                load_lfs_cache_history(offset + 20);
            ImGui::EndDisabled();
            for (const auto &record : lfs_cache_records.at("records")) {
                auto id = record.at("id").get<std::string>();
                ImGui::PushID(id.c_str());
                ImGui::TextWrapped(
                    "%s / %s / %s: %zu / %s", record.at("created").get<std::string>().c_str(),
                    c::tr(record.at("status") == "complete" ? "Complete" : "Incomplete").c_str(),
                    c::tr("Installed objects").c_str(), record.at("installed").get<size_t>(),
                    record.at("source_peer").get<std::string>().c_str());
                if (record.value("acknowledged", false))
                    text(c::tr("Recovery reviewed"));
                ImGui::BeginDisabled(busy());
                if (button("View recovery receipt")) {
                    lfs_recovery_receipt = lfs_recovery_check = nullptr;
                    action("git-lfs-recovery", {{"receipt", id}},
                           [this](const Json &r) { lfs_recovery_receipt = r; });
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            for (const auto &error : lfs_cache_records.at("errors"))
                ImGui::TextWrapped("%s", error.at("error").get<std::string>().c_str());
        }
        ImGui::PopID();
        c::endCard();
        if (!lfs_recovery_receipt.is_null()) {
            c::space(12);
            c::beginCard("lfs-recovery-receipt", c::tr("Recovery receipt").c_str());
            auto id = lfs_recovery_receipt.at("id").get<std::string>();
            text(id);
            text(
                c::tr(lfs_recovery_receipt.at("status") == "complete" ? "Complete" : "Incomplete"));
            if (lfs_recovery_receipt.contains("error"))
                ImGui::TextWrapped("%s",
                                   lfs_recovery_receipt.at("error").get<std::string>().c_str());
            if (lfs_recovery_receipt.contains("acknowledged_at"))
                text(c::tr("Recovery reviewed"));
            ImGui::TextWrapped(
                "%s",
                c::tr(
                    "Review preserves the incomplete status. Missing objects still need recovery.")
                    .c_str());
            ImGui::BeginDisabled(busy());
            if (button("Verify recovery objects")) {
                lfs_recovery_check = nullptr;
                action("git-lfs-recovery-check", {{"receipt", id}},
                       [this](const Json &r) { lfs_recovery_check = r; });
            }
            ImGui::EndDisabled();
            if (!lfs_recovery_check.is_null()) {
                const auto &checked = lfs_recovery_check;
                c::textDim("%s: %zu / %s: %zu / %s: %zu", c::tr("Verified").c_str(),
                           checked.at("verified").get<size_t>(), c::tr("Missing").c_str(),
                           checked.at("missing").get<size_t>(), c::tr("Damaged").c_str(),
                           checked.at("damaged").get<size_t>());
                for (const auto &error : checked.at("errors"))
                    ImGui::TextWrapped("%s", error.get_ref<const std::string &>().c_str());
                ImGui::BeginDisabled(busy() || !checked.at("can_acknowledge").get<bool>());
                if (button("Mark as reviewed")) {
                    Json input{{"receipt", id}, {"plan", checked.at("plan")}};
                    lfs_recovery_check = nullptr;
                    action("git-lfs-recovery-acknowledge", input, [this](const Json &r) {
                        lfs_recovery_receipt["acknowledged_at"] = r.at("acknowledged_at");
                        if (!lfs_cache_records.is_null())
                            for (auto &row : lfs_cache_records["records"])
                                if (row.at("id") == r.at("id"))
                                    row = r;
                    });
                }
                ImGui::EndDisabled();
            }
            ImGui::BeginChild("lfs-recovery-objects", ImVec2(0, 155));
            const auto &objects = lfs_recovery_check.is_null() ? lfs_recovery_receipt.at("objects")
                                                               : lfs_recovery_check.at("objects");
            for (const auto &object : objects) {
                auto state = object.value("state", "");
                auto label = state == "verified"        ? "Verified"
                             : state == "missing"       ? "Missing"
                             : state == "corrupt"       ? "Corrupt"
                             : state == "size_mismatch" ? "Wrong size"
                                                        : "Unsafe path";
                if (state.empty()) {
                    ImGui::TextWrapped(
                        "%s / %llu B", object.at("sha256").get<std::string>().c_str(),
                        static_cast<unsigned long long>(object.at("size").get<uintmax_t>()));
                    continue;
                }
                ImGui::TextWrapped(
                    "%s / %s / %llu B", object.at("sha256").get<std::string>().c_str(),
                    c::tr(label).c_str(),
                    static_cast<unsigned long long>(object.at("size").get<uintmax_t>()));
            }
            ImGui::EndChild();
            c::endCard();
        }
    }
    static std::string rules_text(const Json &rules) {
        std::string result;
        for (const auto &rule : rules) {
            if (!result.empty())
                result += '\n';
            result += rule.get<std::string>();
        }
        return result;
    }
    Json backup_rules() const {
        Json result = Json::array();
        std::istringstream stream(backup_rules_text);
        for (std::string line; std::getline(stream, line);) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                result.push_back(line);
        }
        return result;
    }
    void load_backup_scope(size_t offset = 0, bool continuation = false) {
        auto id = selected;
        auto revision = ++backup_scope_revision;
        Json input{{"excludes", backup_rules()}, {"offset", offset}, {"limit", 100}};
        if (continuation && !backup_scope_state.is_null())
            input["inventory_hash"] = backup_scope_state.at("inventory_hash");
        submit(
            [this, id, input] {
                return core.request("POST", "/api/projects/" + id + "/backup-preview", input);
            },
            [this, id, revision](const Json &r) {
                if (selected == id && revision == backup_scope_revision)
                    backup_scope_state = r;
            });
    }
    void backup_scope_view(const Json &p) {
        if (!backup_rules_loaded) {
            backup_rules_loaded = true;
            backup_rules_text = rules_text(p.at("excludes"));
        }
        c::beginCard("backup-rules", c::tr("Backup exclusions").c_str());
        ImGui::TextWrapped("%s", c::tr("One glob per line. * and ? match paths or path components. "
                                       "Rules also affect local change lists.")
                                     .c_str());
        ImGui::TextWrapped(
            "%s", c::tr("Excluded folders are pruned. Links and recovery originals stay excluded.")
                      .c_str());
        ImGui::BeginDisabled(busy());
        if (ImGui::InputTextMultiline("##backup-rules", &backup_rules_text, ImVec2(-1, 130))) {
            backup_scope_state = nullptr;
            ++backup_scope_revision;
        }
        if (button("Reload saved rules")) {
            backup_rules_text = rules_text(p.at("excludes"));
            backup_scope_state = nullptr;
            ++backup_scope_revision;
        }
        ImGui::SameLine();
        if (button("Preview backup scope", true))
            load_backup_scope();
        ImGui::EndDisabled();
        c::endCard();
        if (backup_scope_state.is_null())
            return;
        const auto &preview = backup_scope_state;
        c::space(12);
        c::beginCard("backup-inventory", c::tr("Backup preview").c_str());
        c::textDim("%s: %zu / %llu B / %s: %zu", c::tr("Included files").c_str(),
                   preview.at("files").get<size_t>(),
                   static_cast<unsigned long long>(preview.at("bytes").get<uintmax_t>()),
                   c::tr("Skipped entries").c_str(), preview.at("skipped").get<size_t>());
        ImGui::TextWrapped("%s", c::tr("Metadata preview; file contents are read when backing up. "
                                       "Concurrent edits can change the result.")
                                     .c_str());
        ImGui::TextWrapped(
            "%s", c::tr("Storage estimates exclude compression, deduplication and temporary space.")
                      .c_str());
        ImGui::BeginDisabled(busy() || !preview.at("rules_changed").get<bool>());
        if (button("Save reviewed rules", true)) {
            auto id = selected;
            auto revision = ++backup_scope_revision;
            auto input = Json{{"excludes", preview.at("excludes")}, {"plan", preview.at("plan")}};
            submit(
                [this, id, input] {
                    return core.request("PUT", "/api/projects/" + id + "/backup-excludes", input);
                },
                [this, id, revision](const Json &) {
                    if (id == selected && revision == backup_scope_revision)
                        backup_scope_state = nullptr;
                    refresh();
                });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(busy() || preview.at("rules_changed").get<bool>());
        if (button("Back up reviewed scope")) {
            action("backup", {{"plan", preview.at("plan")}},
                   [this](const Json &) { backup_scope_state = nullptr; });
        }
        ImGui::EndDisabled();
        auto offset = preview.at("offset").get<size_t>();
        ImGui::BeginDisabled(busy() || offset == 0);
        if (button("Previous page"))
            load_backup_scope(offset >= 100 ? offset - 100 : 0, true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(busy() || !preview.at("has_more").get<bool>());
        if (button("Next page"))
            load_backup_scope(offset + 100, true);
        ImGui::EndDisabled();
        ImGui::BeginChild("backup-inventory-rows", ImVec2(0, 250));
        for (const auto &row : preview.at("entries")) {
            bool included = row.at("included");
            auto reason = included ? c::tr("Included")
                                   : c::tr(row.at("reason") == "rule"        ? "Matched rule"
                                           : row.at("reason") == "protected" ? "Recovery originals"
                                           : row.at("reason") == "link"      ? "Link"
                                                                             : "Special file");
            if (row.contains("rule"))
                reason += ": " + row.at("rule").get<std::string>();
            std::string detail;
            if (included) {
                auto storage = row.at("storage").get<std::string>();
                detail = " / " + row.at("bytes").dump() + " B / " +
                         c::tr(storage == "blocks-v1"       ? "Fixed blocks (4 MiB)"
                               : storage == "blocks-cdc-v1" ? "Content-defined blocks (1–8 MiB)"
                                                            : "Whole files");
            }
            ImGui::TextWrapped("%s / %s%s", row.at("path").get<std::string>().c_str(),
                               reason.c_str(), detail.c_str());
        }
        ImGui::EndChild();
        c::endCard();
    }
    void load_mode_options() {
        auto id = selected;
        auto revision = ++mode_options_revision;
        mode_options_requested = true;
        submit([this, id] { return core.request("GET", "/api/projects/" + id + "/mode-options"); },
               [this, id, revision](const Json &r) {
                   if (selected == id && mode_options_revision == revision) {
                       mode_options_state = r;
                       mode_options_saved = false;
                   }
               });
    }
    void mode_options_view(const Json &project_value) {
        for (const auto &mode : overview.at("modes"))
            if (mode.at("id") == project_value.at("mode") && mode.value("plugin", Json()).is_null())
                return;
        c::beginCard("mode-options", c::tr("Mode options").c_str());
        ImGui::TextWrapped("%s", c::tr("These settings belong to this project.").c_str());
        ImGui::BeginDisabled(busy());
        bool reload = button("Reload mode options");
        ImGui::EndDisabled();
        if ((reload || !mode_options_requested) && !busy())
            load_mode_options();
        if (!mode_options_state.is_null()) {
            const auto &config = mode_options_state;
            if (!config.at("available").get<bool>())
                text(c::tr("Load the plugin providing this mode."));
            else {
                if (config.at("fields").empty())
                    text(c::tr("No configurable options"));
                for (const auto &issue : config.at("errors"))
                    ImGui::TextWrapped("%s", issue.get<std::string>().c_str());
                for (const auto &field : config.at("fields")) {
                    auto id = field.at("id").get<std::string>();
                    ImGui::PushID(id.c_str());
                    text(plugin_label(field, "label"));
                    bool edited = false;
                    auto type = field.at("type").get<std::string>();
                    if (type == "boolean") {
                        bool value = config.at("values").at(id);
                        edited = c::toggleSwitch("##value", &value);
                        if (edited)
                            mode_options_state["values"][id] = value;
                    } else if (type == "integer") {
                        int value = config.at("values").at(id);
                        edited = ImGui::InputInt("##value", &value);
                        if (edited)
                            mode_options_state["values"][id] = value;
                        c::textDim("%s: %lld / %lld", c::tr("Range").c_str(),
                                   static_cast<long long>(field.at("minimum").get<int64_t>()),
                                   static_cast<long long>(field.at("maximum").get<int64_t>()));
                    } else {
                        auto value = config.at("values").at(id).get<std::string>();
                        edited = ImGui::InputText("##value", &value);
                        if (edited)
                            mode_options_state["values"][id] = value;
                        c::textDim("%s: %u", c::tr("Maximum UTF-8 bytes").c_str(),
                                   field.at("max_length").get<unsigned>());
                    }
                    if (edited) {
                        ++mode_options_revision;
                        mode_options_saved = false;
                    }
                    auto description = plugin_label(field, "description");
                    if (!description.empty())
                        ImGui::TextWrapped("%s", description.c_str());
                    ImGui::PopID();
                    c::space(6);
                }
                ImGui::BeginDisabled(
                    busy() || (config.at("fields").empty() && config.at("valid").get<bool>()));
                if (button("Use defaults")) {
                    mode_options_state["values"] = config.at("defaults");
                    ++mode_options_revision;
                    mode_options_saved = false;
                }
                ImGui::SameLine();
                if (button("Save mode options", true)) {
                    auto id = selected;
                    auto revision = ++mode_options_revision;
                    Json input{{"plan", config.at("plan")}, {"values", config.at("values")}};
                    submit(
                        [this, id, input] {
                            return core.request("PUT", "/api/projects/" + id + "/mode-options",
                                                input);
                        },
                        [this, id, revision](const Json &r) {
                            if (id == selected && revision == mode_options_revision) {
                                mode_options_state = r;
                                mode_options_saved = true;
                                refresh();
                            }
                        });
                }
                ImGui::EndDisabled();
                if (mode_options_saved)
                    c::badge(c::tr("Mode options saved").c_str(), c::Tone::success);
            }
        }
        c::endCard();
        c::space(12);
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
    void open_help(const std::string &context = "") {
        if (help_pending.valid())
            return;
        auto topic = context.empty() ? view : context;
        auto locale = language;
        help_pending = std::async(std::launch::async, [topic, locale] {
            auto target = cysynck::help::context(topic, locale);
            cysynck::help::open(target);
            return target;
        });
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
    Json restore_request() const {
        Json data{{"snapshot", restore_id}, {"destination", destination}};
        Json paths = Json::array();
        std::istringstream input(restore_paths);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty())
                paths.push_back(line);
        }
        if (!paths.empty())
            data["paths"] = paths;
        if (!backup_files.is_null() && backup_files.at("snapshot") == restore_id)
            data["manifest_hash"] = backup_files.at("manifest_hash");
        return data;
    }
    void browse_backup(size_t offset, bool continuation = false) {
        auto snapshot = restore_id, prefix = backup_prefix;
        Json query{{"snapshot", snapshot}, {"prefix", prefix}, {"offset", offset}};
        if (continuation && !backup_files.is_null())
            query["manifest_hash"] = backup_files.at("manifest_hash");
        action("snapshot-files", query, [this, snapshot, prefix](const Json &r) {
            if (restore_id == snapshot && backup_prefix == prefix)
                backup_files = r;
        });
    }
    void compare_snapshots(size_t offset, bool continuation = false) {
        auto left = history_left, right = history_right;
        Json query{{"left", left}, {"right", right}, {"offset", offset}};
        if (continuation && !snapshot_comparison.is_null()) {
            query["left_hash"] = snapshot_comparison.at("left_hash");
            query["right_hash"] = snapshot_comparison.at("right_hash");
        }
        action("snapshot-compare", query, [this, left, right](const Json &r) {
            if (left == history_left && right == history_right) {
                snapshot_comparison = r;
                snapshot_file_diff = nullptr;
            }
        });
    }
    void saved_snapshot_selector(const char *label, std::string &value) {
        if (ImGui::BeginCombo(c::trLabel(label).c_str(), value.c_str())) {
            for (const auto &s : snapshots)
                if (ImGui::Selectable(
                        (s.at("created").get<std::string>() + " / " + s.at("id").get<std::string>())
                            .c_str(),
                        s.at("id") == value)) {
                    value = s.at("id");
                    snapshot_comparison = snapshot_file_diff = nullptr;
                }
            ImGui::EndCombo();
        }
    }
    void saved_files_view() {
        c::beginCard("backup-browser", c::tr("Browse saved files").c_str());
        ImGui::TextWrapped(
            "%s",
            c::tr("Metadata only; file contents are verified when previewed or restored.").c_str());
        ImGui::TextUnformatted(c::tr("Folder prefix").c_str());
        if (ImGui::InputText("##backup-prefix", &backup_prefix))
            backup_files = nullptr;
        if (button("Browse saved files"))
            browse_backup(0);
        ImGui::SameLine();
        ImGui::BeginDisabled(backup_prefix.empty());
        if (button("Add folder to selection")) {
            restore_paths += backup_prefix + (backup_prefix.back() == '/' ? "\n" : "/\n");
            restore_preview = nullptr;
        }
        ImGui::EndDisabled();
        if (!backup_files.is_null()) {
            text(c::tr("Files") + ": " + backup_files.at("total").dump());
            auto offset = backup_files.at("offset").get<size_t>();
            ImGui::BeginDisabled(offset == 0);
            if (button("Previous page"))
                browse_backup(offset > 100 ? offset - 100 : 0, true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!backup_files.at("has_more").get<bool>());
            if (button("Next page"))
                browse_backup(offset + 100, true);
            ImGui::EndDisabled();
            ImGui::BeginChild("backup-page", c::dp(0, 180), ImGuiChildFlags_Borders);
            for (const auto &entry : backup_files.at("files")) {
                auto path = entry.at("path").get<std::string>();
                if (ImGui::Selectable((path + " / " + entry.at("size").dump() + " B").c_str())) {
                    restore_paths += path + "\n";
                    restore_preview = nullptr;
                }
            }
            ImGui::EndChild();
        }
        c::endCard();
    }
    void saved_comparison_view() {
        if (snapshots.empty())
            return;
        if (history_left.empty())
            history_left = snapshots.back().at("id");
        if (history_right.empty())
            history_right = snapshots.front().at("id");
        c::beginCard("snapshot-comparison", c::tr("Compare saved versions").c_str());
        saved_snapshot_selector("Older snapshot", history_left);
        saved_snapshot_selector("Newer snapshot", history_right);
        if (button("Compare snapshots"))
            compare_snapshots(0);
        if (!snapshot_comparison.is_null()) {
            text(c::tr("Files") + ": " + snapshot_comparison.at("total").dump());
            auto offset = snapshot_comparison.at("offset").get<size_t>();
            ImGui::BeginDisabled(offset == 0);
            if (button("Previous page"))
                compare_snapshots(offset > 100 ? offset - 100 : 0, true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!snapshot_comparison.at("has_more").get<bool>());
            if (button("Next page"))
                compare_snapshots(offset + 100, true);
            ImGui::EndDisabled();
            ImGui::BeginChild("saved-changes", c::dp(0, 150), ImGuiChildFlags_Borders);
            for (const auto &entry : snapshot_comparison.at("files")) {
                auto path = entry.at("path").get<std::string>();
                auto status = entry.at("status").get<std::string>();
                auto label = c::tr(status == "added"      ? "Added"
                                   : status == "deleted"  ? "Deleted"
                                   : status == "modified" ? "Modified"
                                                          : "Permissions changed");
                if (ImGui::Selectable((label + " / " + path).c_str())) {
                    Json request{{"left", history_left},
                                 {"right", history_right},
                                 {"path", path},
                                 {"left_hash", snapshot_comparison.at("left_hash")},
                                 {"right_hash", snapshot_comparison.at("right_hash")}};
                    action("snapshot-compare-file", request, [this, request](const Json &r) {
                        if (!snapshot_comparison.is_null() &&
                            snapshot_comparison.at("left_hash") == request.at("left_hash") &&
                            snapshot_comparison.at("right_hash") == request.at("right_hash"))
                            set_comparison(r, snapshot_file_diff, backup_document);
                    });
                }
            }
            ImGui::EndChild();
        }
        if (!snapshot_file_diff.is_null()) {
            if (snapshot_file_diff.at("permissions_changed").get<bool>())
                text(c::tr("Permissions changed"));
            comparison_view("saved-file-diff", snapshot_file_diff, backup_document,
                            backup_diff_view);
        } else
            ImGui::TextWrapped("%s", c::tr("Select a file to inspect its saved versions.").c_str());
        c::endCard();
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
                backup_files = nullptr;
                restore_paths.clear();
            }
            if (s.at("id") == restore_id && s.contains("block_storage")) {
                const auto &stats = s.at("block_storage");
                c::textDim("%s: %llu KiB / %s: %llu KiB", c::tr("Block data written").c_str(),
                           static_cast<unsigned long long>(
                               stats.at("new_block_bytes").get<uintmax_t>() / 1024),
                           c::tr("Block data reused").c_str(),
                           static_cast<unsigned long long>(
                               stats.at("reused_block_bytes").get<uintmax_t>() / 1024));
                if (stats.value("content_defined_files", uintmax_t{0}))
                    text(c::tr("Content-defined file versions") + ": " +
                         stats.at("content_defined_files").dump());
            }
        }
        if (!restore_id.empty()) {
            c::space(12);
            saved_files_view();
            c::beginCard("restore", c::tr("Restore to new directory").c_str());
            ImGui::TextWrapped(
                "%s",
                c::tr("Preview verifies the selected files before creating a new destination.")
                    .c_str());
            ImGui::TextUnformatted(c::tr("Restore selection").c_str());
            ImGui::TextWrapped("%s", c::tr("One relative path per line; folders end with /. Leave "
                                           "empty for the whole snapshot.")
                                         .c_str());
            if (ImGui::InputTextMultiline("##restore-paths", &restore_paths, c::dp(-1, 90)))
                restore_preview = nullptr;
            if (button("Clear selection")) {
                restore_paths.clear();
                restore_preview = nullptr;
            }
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
                auto requested = restore_request();
                action("restore-plan", requested, [this, requested](const Json &r) {
                    if (requested != restore_request())
                        return;
                    restore_preview = r;
                    restore_preview["input"] = requested;
                });
            }
            ImGui::EndDisabled();
            bool preview_current =
                !restore_preview.is_null() && restore_preview.at("input") == restore_request();
            ImGui::SameLine();
            ImGui::BeginDisabled(!preview_current || !restore_preview.value("ok", false));
            if (button("Restore to new directory", true)) {
                auto request = restore_request();
                request["plan"] = restore_preview.at("plan");
                action("restore", request, [this](const Json &) { restore_preview = nullptr; });
            }
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
            saved_comparison_view();
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
                                      {"chunked_files", "Chunked file versions"},
                                      {"content_defined_files", "Content-defined file versions"}}) {
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
    Json git_context() const {
        Json query;
        for (auto key : {"peer", "branch", "base", "head", "local_ref"})
            query[key] = git_review_state.at(key);
        return query;
    }
    bool current_git_context(const Json &result) const {
        return !git_review_state.is_null() && result.at("base") == git_review_state.at("base") &&
               result.at("head") == git_review_state.at("head") &&
               result.at("local_ref") == git_review_state.at("local_ref") &&
               result.at("tracking_ref") == git_review_state.at("tracking_ref");
    }
    void clear_git_merge_choices() {
        git_merge_choices.clear();
        git_merge_edits.clear();
        git_merge_editor = nullptr;
        git_merge_buffer.clear();
        git_region_choices.clear();
    }
    void git_merge_editor_view() {
        if (git_merge_editor.is_null())
            return;
        auto path = git_merge_editor.at("path").get<std::string>();
        text(c::tr("Edit merged text") + ": " + path);
        text(c::tr("Remove conflict markers and review the final text. Maximum 512 KiB; edits are "
                   "saved in the private receipt."));
        if (ImGui::BeginTabBar("edited-git-versions")) {
            for (auto [key, title] : {std::pair{"ancestor", "Common ancestor"},
                                      {"local", "Local committed version"},
                                      {"peer", "Peer committed version"}}) {
                auto &texts = git_merge_editor.at("texts");
                if (!texts.contains(key) || !texts.at(key).is_string())
                    continue;
                if (ImGui::BeginTabItem(c::tr(title).c_str())) {
                    auto &value = texts.at(key).get_ref<std::string &>();
                    ImGui::InputTextMultiline("##edited-source", &value, c::dp(-1, 130),
                                              ImGuiInputTextFlags_ReadOnly);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        const auto &regions = git_merge_editor.at("regions");
        if (regions.at("available").get<bool>()) {
            if (ImGui::CollapsingHeader(c::tr("Choose conflict regions").c_str())) {
                text(c::tr("Building a draft replaces the editor text. Review it before use."));
                ImGui::BeginChild("git-region-choices", c::dp(0, 260), ImGuiChildFlags_Borders);
                for (const auto &part : regions.at("segments")) {
                    if (part.at("kind") != "conflict")
                        continue;
                    auto id = part.at("id").get<size_t>();
                    ImGui::PushID(static_cast<int>(id));
                    text(c::tr("Conflict region") + " " + std::to_string(id + 1));
                    for (auto [key, title] : {std::pair{"ancestor", "Common ancestor"},
                                              {"local", "Local committed version"},
                                              {"peer", "Peer committed version"}}) {
                        if (ImGui::TreeNode(key, "%s", c::tr(title).c_str())) {
                            auto value = part.at(key).get<std::string>();
                            ImGui::InputTextMultiline("##region-source", &value, c::dp(-1, 90),
                                                      ImGuiInputTextFlags_ReadOnly);
                            ImGui::TreePop();
                        }
                    }
                    ImGui::BeginDisabled(busy());
                    for (auto [key, title] : {std::pair{"local", "Use local region"},
                                              {"peer", "Use peer region"},
                                              {"both", "Use both (local then peer)"}})
                        if (ImGui::RadioButton(c::tr(title).c_str(),
                                               git_region_choices.at(id) == key))
                            git_region_choices.at(id) = key;
                    ImGui::EndDisabled();
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
                bool complete = std::all_of(git_region_choices.begin(), git_region_choices.end(),
                                            [](const auto &choice) { return !choice.empty(); });
                ImGui::BeginDisabled(busy() || !complete);
                if (button("Build draft from selected regions")) {
                    auto input = git_context();
                    input["path"] = path;
                    input["merge_base"] = git_merge_editor.at("merge_base");
                    input["region_token"] = regions.at("token");
                    input["region_choices"] = git_region_choices;
                    auto token = regions.at("token");
                    action("git-merge-file", input, [this, path, token](const Json &r) {
                        if (current_git_context(r) && !git_merge_editor.is_null() &&
                            git_merge_editor.at("path") == path &&
                            git_merge_editor.at("regions").at("token") == token)
                            git_merge_buffer = r.at("composed_text").get<std::string>();
                    });
                }
                ImGui::EndDisabled();
            }
        } else if (git_merge_editor.at("kind") == "conflict")
            text(
                c::tr("Region choices are unavailable for this proposal; edit the text manually."));
        ImGui::BeginDisabled(busy());
        ImGui::InputTextMultiline("##edited-merge-content", &git_merge_buffer, c::dp(-1, 240));
        ImGui::EndDisabled();
        text(c::tr("Edited text bytes") + ": " + std::to_string(git_merge_buffer.size()));
        ImGui::BeginDisabled(busy() || git_merge_buffer.size() > 512 * 1024);
        if (button("Use edited text", true)) {
            git_merge_edits[path] = git_merge_buffer;
            git_merge_choices[path] = "edited";
            git_integrate_input = nullptr;
            git_merge_editor = nullptr;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (button("Cancel edit"))
            git_merge_editor = nullptr;
    }
    void load_git_history(uintmax_t offset = 0) {
        if (busy())
            return;
        auto query = git_context();
        query["offset"] = offset;
        action("git-history", query, [this](const Json &r) {
            if (!current_git_context(r))
                return;
            git_history_state = r;
            git_commit_state = git_review_diff = nullptr;
        });
    }
    void load_git_commit(const std::string &oid, unsigned parent = 0) {
        if (busy())
            return;
        auto query = git_context();
        query["commit"] = oid;
        if (parent)
            query["parent"] = parent;
        action("git-commit", query, [this](const Json &r) {
            if (!current_git_context(r))
                return;
            git_commit_state = r;
            git_review_diff = nullptr;
        });
    }
    void git_merge_view() {
        c::beginCard("git-three-way", c::tr("Three-way Git review").c_str());
        ImGui::TextWrapped(
            "%s", c::tr("Compares committed files with their common ancestor. Proposals do not "
                        "apply a repository merge or include working changes and rename detection.")
                      .c_str());
        ImGui::BeginDisabled(busy());
        if (button("Review three-way changes")) {
            git_merge_state = git_merge_file_state = nullptr;
            action("git-merge-review", git_context(), [this](const Json &r) {
                if (current_git_context(r))
                    git_merge_state = r;
            });
        }
        ImGui::EndDisabled();
        if (!git_merge_state.is_null()) {
            const auto reasons = std::map<std::string, const char *>{
                {"unborn", "Three-way review needs a local commit"},
                {"unrelated", "Three-way review needs a shared ancestor"},
                {"multiple_bases", "Multiple merge bases require a repository merge"}};
            if (!git_merge_state.value("can_review", false))
                text(c::tr(reasons.at(git_merge_state.at("reason").get<std::string>())));
            else {
                text(c::tr("Common ancestor") + ": " +
                     git_merge_state.at("merge_base").get<std::string>());
                const auto classes =
                    std::map<std::string, const char *>{{"local_only", "Local change only"},
                                                        {"peer_only", "Peer change only"},
                                                        {"identical", "Same change on both sides"},
                                                        {"both_changed", "Changed on both sides"},
                                                        {"structural", "Structural conflict"}};
                if (git_merge_state.at("files").empty())
                    text(c::tr("No changed files"));
                ImGui::BeginChild("three-way-paths", c::dp(0, 200), ImGuiChildFlags_Borders);
                for (const auto &entry : git_merge_state.at("files")) {
                    auto path = entry.at("path").get<std::string>();
                    ImGui::PushID(path.c_str());
                    text(path + " / " +
                         c::tr(classes.at(entry.at("classification").get<std::string>())));
                    ImGui::BeginDisabled(busy());
                    if (button("Preview three-way file")) {
                        auto input = git_context();
                        input["path"] = path;
                        input["merge_base"] = git_merge_state.at("merge_base");
                        git_merge_file_state = nullptr;
                        action("git-merge-file", input, [this](const Json &r) {
                            if (current_git_context(r) && !git_merge_state.is_null() &&
                                git_merge_state.at("merge_base") == r.at("merge_base"))
                                git_merge_file_state = r;
                        });
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        if (!git_merge_file_state.is_null()) {
            auto &draft = git_merge_file_state;
            text(draft.at("path"));
            const auto kinds =
                std::map<std::string, const char *>{{"clean", "Text proposal without conflicts"},
                                                    {"conflict", "Text conflicts detected"},
                                                    {"delete", "Proposed file deletion"},
                                                    {"manual", "Manual Git review required"}};
            text(c::tr(kinds.at(draft.at("kind").get<std::string>())));
            if (draft.at("kind") == "manual") {
                const auto reasons = std::map<std::string, const char *>{
                    {"structure", "Review file types and path collisions in Git"},
                    {"submodule", "Review submodules in Git"},
                    {"symlink", "Review symbolic links in Git"},
                    {"file_type", "Review file types and path collisions in Git"},
                    {"large", "Three-way text previews are limited to 512 KiB per side"},
                    {"binary", "Binary or non-UTF-8 content needs manual review"},
                    {"lfs_pointer", "LFS assets need manual Git review"},
                    {"attributes", "Custom merge or checkout attributes need manual Git review"},
                    {"modify_delete", "One side modified a file deleted by the other"},
                    {"permissions", "File permissions need manual Git review"},
                    {"output_limit", "Merge proposal exceeds its preview bound"}};
                text(c::tr(reasons.at(draft.at("reason").get<std::string>())));
            }
            if (!draft.at("conflicts").is_null())
                text(c::tr("Conflict regions reported") + ": " + draft.at("conflicts").dump());
            ImGui::PushID(draft.at("path").get_ref<const std::string &>().c_str());
            if (ImGui::BeginTabBar("three-way-text-tabs")) {
                for (auto [key, title] : {std::pair{"candidate", "Merge proposal"},
                                          {"ancestor", "Common ancestor"},
                                          {"local", "Local committed version"},
                                          {"peer", "Peer committed version"}}) {
                    auto *value = std::string(key) == "candidate"   ? &draft.at("candidate")
                                  : draft.at("texts").contains(key) ? &draft.at("texts").at(key)
                                                                    : nullptr;
                    if (!value || value->is_null())
                        continue;
                    if (ImGui::BeginTabItem(c::tr(title).c_str())) {
                        auto &contents = value->get_ref<std::string &>();
                        ImGui::InputTextMultiline("##three-way-content", &contents, c::dp(-1, 220),
                                                  ImGuiInputTextFlags_ReadOnly);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::PopID();
        }
        c::endCard();
    }
    void git_materialization_receipts_view() {
        c::beginCard("lfs-receipts", c::tr("LFS materialization receipts").c_str());
        ImGui::TextWrapped(
            "%s", c::tr("Receipt checks preserve working files, staged assets and original "
                        "pointers. Mark reviewed only after an exact before or after check.")
                      .c_str());
        auto load = [this](size_t offset) {
            auto project = selected;
            git_materialization_check = nullptr;
            git_materialization_resume = nullptr;
            git_asset_export_state = nullptr;
            action("git-materializations", {{"offset", offset}},
                   [this, project, offset](const Json &r) {
                       if (selected == project) {
                           git_materialization_records = r;
                           git_materialization_offset = offset;
                       }
                   });
        };
        if (button("Load LFS receipts"))
            load(0);
        ImGui::TextWrapped("%s", c::tr("Choose a new absolute directory outside all projects and "
                                       "the profile.")
                                     .c_str());
        ImGui::BeginDisabled(busy());
        if (ImGui::InputText(c::tr("LFS asset export directory").c_str(),
                             &git_asset_export_destination))
            git_asset_export_state = nullptr;
        ImGui::EndDisabled();
        if (!git_materialization_records.is_null()) {
            ImGui::BeginChild("lfs-receipts-list", c::dp(0, 220), ImGuiChildFlags_Borders);
            if (git_materialization_records.at("records").empty())
                text(c::tr("No LFS receipts"));
            for (const auto &record : git_materialization_records.at("records")) {
                bool inverse = record.value("operation", "materialize") == "dehydrate";
                auto id = record.at("id").get<std::string>();
                ImGui::PushID(id.c_str());
                text(record.at("created").get<std::string>() + " / " + id);
                text(c::tr(record.at("status") == "complete"
                               ? (inverse ? "LFS pointer checkout complete"
                                          : "LFS materialization complete")
                               : (inverse ? "LFS pointer checkout incomplete"
                                          : "LFS materialization incomplete")));
                text(c::tr("Recovery directory") + ": " +
                     record.at("recovery_directory").get<std::string>());
                text(c::tr("Excluded LFS pointers") + ": " + record.at("excluded").dump());
                if (!record.at("selection").is_null() &&
                    ImGui::TreeNode(c::tr("LFS selection").c_str())) {
                    for (const auto &path : record.at("selection"))
                        text(path.get<std::string>());
                    ImGui::TreePop();
                }
                if (record.at("acknowledged").get<bool>())
                    text(c::tr("Recovery reviewed"));
                if (!record.at("resume").is_null())
                    text(c::tr(record.at("resume").at("status") == "complete"
                                   ? "LFS publication resumed"
                                   : "LFS publication resume incomplete"));
                if (button("Check LFS receipt")) {
                    auto project = selected;
                    git_materialization_check = nullptr;
                    git_materialization_resume = nullptr;
                    action("git-materialization-check", {{"receipt", id}},
                           [this, project](const Json &r) {
                               if (selected == project)
                                   git_materialization_check = r;
                           });
                }
                if (!git_materialization_check.is_null() &&
                    git_materialization_check.at("record").at("id") == id) {
                    const auto &checked = git_materialization_check;
                    if (checked.at("context_matches").get<bool>() &&
                        !checked.value("index_metadata_matches", true))
                        ImGui::TextWrapped(
                            "%s", c::tr("Index metadata refreshed; staged entries and flags match")
                                      .c_str());
                    bool valid =
                        checked.at("errors").empty() && checked.at("context_matches").get<bool>();
                    text(c::tr(valid && checked.at("position") == "after"
                                   ? (inverse ? "LFS pointers and preserved assets verified"
                                              : "LFS assets and recovery pointers verified")
                               : valid && checked.at("position") == "before"
                                   ? (inverse ? "Original LFS assets verified"
                                              : "LFS files match the original pointers")
                               : checked.at("can_resume").get<bool>()
                                   ? "LFS recovery can be resumed"
                                   : "LFS recovery needs manual inspection"));
                    for (const auto &file : checked.at("files"))
                        text(file.at("path").get<std::string>() + " / " +
                             c::tr(file.at("position") == "before"
                                       ? (inverse ? "Verified asset" : "Original pointer")
                                   : file.at("position") == "after"
                                       ? (inverse ? "Committed pointer" : "Verified asset")
                                   : file.at("position") == "interrupted"
                                       ? "Publication interrupted"
                                       : "Local contents differ"));
                    for (const auto &error : checked.at("errors"))
                        ImGui::TextWrapped("%s", error.get_ref<const std::string &>().c_str());
                    for (const auto &error : checked.at("resume_errors"))
                        text(error.at("path").get<std::string>() + ": " +
                             error.at("error").get<std::string>());
                    if (checked.at("can_resume").get<bool>() && button("Preview LFS resume")) {
                        auto project = selected;
                        git_materialization_resume = nullptr;
                        action("git-materialization-resume", {{"receipt", id}},
                               [this, project](const Json &r) {
                                   if (selected == project)
                                       git_materialization_resume = r;
                               });
                    }
                    ImGui::BeginDisabled(busy() || !checked.at("can_acknowledge").get<bool>());
                    if (button("Mark as reviewed")) {
                        auto project = selected;
                        Json input{{"receipt", id}, {"plan", checked.at("plan")}};
                        git_materialization_check = nullptr;
                        git_materialization_resume = nullptr;
                        action("git-materialization-acknowledge", input,
                               [this, project, id](const Json &r) {
                                   if (selected == project &&
                                       !git_materialization_records.is_null())
                                       for (auto &row : git_materialization_records["records"])
                                           if (row.at("id") == id)
                                               row = r;
                               });
                    }
                    ImGui::EndDisabled();
                }
                if (!git_materialization_resume.is_null() &&
                    git_materialization_resume.at("receipt") == id) {
                    const auto &resume = git_materialization_resume;
                    text(c::tr("Already published assets") + ": " +
                         resume.at("already_published").dump());
                    text(c::tr("Pending publications") + ": " +
                         resume.at("pending_publications").dump());
                    text(c::tr("Assets to stage from cache") + ": " +
                         resume.at("pending_staging").dump());
                    text(c::tr("Staging bytes required") + ": " +
                         resume.at("staging_bytes").dump());
                    text(c::tr("Pointers to preserve") + ": " +
                         resume.at("pointers_to_preserve").dump());
                    for (const auto &error : resume.at("check").at("errors"))
                        text(error.get<std::string>());
                    for (const auto &error : resume.at("check").at("resume_errors"))
                        text(error.at("path").get<std::string>() + ": " +
                             error.at("error").get<std::string>());
                    ImGui::BeginDisabled(busy() || !resume.at("can_resume").get<bool>());
                    if (button("Resume verified publication", true)) {
                        auto project = selected;
                        Json input{{"receipt", id}, {"apply", true}, {"plan", resume.at("plan")}};
                        git_materialization_resume = git_materialization_check = nullptr;
                        action("git-materialization-resume", input,
                               [this, project, id](const Json &r) {
                                   if (selected == project &&
                                       !git_materialization_records.is_null())
                                       for (auto &row : git_materialization_records["records"])
                                           if (row.at("id") == id) {
                                               row["status"] = r.at("status");
                                               row["files_written"] = r.at("files_written");
                                               row["resume"] = r.at("resume");
                                               row["needs_attention"] = false;
                                           }
                               });
                    }
                    ImGui::EndDisabled();
                }
                if (inverse && button("Preview preserved asset export")) {
                    auto project = selected;
                    auto destination = git_asset_export_destination;
                    git_asset_export_state = nullptr;
                    action("git-materialization-export",
                           {{"receipt", id}, {"destination", destination}},
                           [this, project, destination](const Json &r) {
                               if (selected == project &&
                                   git_asset_export_destination == destination)
                                   git_asset_export_state = r;
                           });
                }
                if (!git_asset_export_state.is_null() &&
                    git_asset_export_state.at("receipt") == id) {
                    const auto &exported = git_asset_export_state;
                    text(c::tr("Assets to export") + ": " +
                         std::to_string(exported.at("files").size()));
                    text(c::tr("Asset bytes") + ": " + exported.at("bytes").dump());
                    if (ImGui::TreeNode(c::tr("Assets to export").c_str())) {
                        for (const auto &file : exported.at("files"))
                            text(file.at("path").get<std::string>());
                        ImGui::TreePop();
                    }
                    for (const auto &issue : exported.at("errors"))
                        text(issue.at("path").get<std::string>() + ": " +
                             issue.at("error").get<std::string>());
                    if (exported.at("exported").get<bool>())
                        text(c::tr("Preserved asset export complete") + ": " +
                             exported.at("destination").get<std::string>());
                    ImGui::BeginDisabled(busy() || !exported.at("can_export").get<bool>());
                    if (button("Export verified preserved assets", true)) {
                        auto project = selected;
                        auto destination = git_asset_export_destination;
                        Json input{{"receipt", id},
                                   {"destination", destination},
                                   {"apply", true},
                                   {"plan", exported.at("plan")}};
                        git_asset_export_state = nullptr;
                        action("git-materialization-export", input,
                               [this, project, destination](const Json &r) {
                                   if (selected == project &&
                                       git_asset_export_destination == destination)
                                       git_asset_export_state = r;
                               });
                    }
                    ImGui::EndDisabled();
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();
            for (const auto &issue : git_materialization_records.at("errors"))
                text(issue.at("id").get<std::string>() + ": " +
                     issue.at("error").get<std::string>());
            for (const auto &name : git_materialization_records.at("unindexed"))
                text(name);
            ImGui::BeginDisabled(busy() || git_materialization_offset == 0);
            if (button("Previous receipts"))
                load(git_materialization_offset >= 20 ? git_materialization_offset - 20 : 0);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(busy() || !git_materialization_records.at("has_more").get<bool>());
            if (button("Next receipts"))
                load(git_materialization_offset + 20);
            ImGui::EndDisabled();
        }
        c::endCard();
    }
    void git_materialize_view() {
        c::beginCard("lfs-materialization", c::tr("Local LFS assets").c_str());
        ImGui::BeginDisabled(busy());
        if (ImGui::Checkbox(c::tr("Prepare pointers for Git updates").c_str(), &git_dehydrate))
            git_materialize_state = nullptr;
        ImGui::EndDisabled();
        if (git_dehydrate)
            ImGui::TextWrapped("%s",
                               c::tr("Preserve unchanged expanded assets in recovery storage and "
                                     "restore their committed pointers before Git integration.")
                                   .c_str());
        else
            ImGui::TextWrapped("%s",
                               c::tr("Replace exact committed pointers with verified cached "
                                     "assets. Modified, missing and staged files are preserved.")
                                   .c_str());
        ImGui::TextUnformatted(c::tr("LFS selection").c_str());
        ImGui::TextWrapped(
            "%s",
            c::tr(
                "One relative path per line; folders end with /. Leave empty for all LFS pointers.")
                .c_str());
        ImGui::BeginDisabled(busy());
        if (ImGui::InputTextMultiline("##lfs-paths", &git_materialization_paths, c::dp(-1, 90)))
            git_materialize_state = nullptr;
        ImGui::EndDisabled();
        if (button(git_dehydrate ? "Preview LFS pointer checkout"
                                 : "Preview LFS materialization")) {
            git_materialize_state = nullptr;
            Json input = Json::object(), paths = Json::array();
            std::istringstream lines(git_materialization_paths);
            for (std::string line; std::getline(lines, line);) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty())
                    paths.push_back(line);
            }
            if (!paths.empty())
                input["paths"] = paths;
            if (git_dehydrate)
                input["operation"] = "dehydrate";
            action("git-materialize", input,
                   [this, paths_text = git_materialization_paths,
                    inverse = git_dehydrate](const Json &r) {
                       if (r.at("project") == selected && paths_text == git_materialization_paths &&
                           inverse == git_dehydrate)
                           git_materialize_state = r;
                   });
        }
        if (!git_materialize_state.is_null()) {
            const auto &r = git_materialize_state;
            for (auto [key, title] : {std::pair{"pending", "Pending assets"},
                                      {"materialized", "Already materialized"},
                                      {"skipped", "Preserved files"},
                                      {"excluded", "Excluded LFS pointers"},
                                      {"bytes", "Asset bytes"}})
                if (git_dehydrate && std::string(key) == "materialized")
                    text(c::tr("Already committed pointers") + ": " + r.at("pointers").dump());
                else
                    text(c::tr(git_dehydrate && std::string(key) == "pending" ? "Assets to preserve"
                                                                              : title) +
                         ": " + r.at(key).dump());
            if (r.value("applied", false)) {
                text(c::tr(git_dehydrate ? "LFS pointer checkout complete"
                                         : "LFS materialization complete"));
                text(r.at("id"));
                text(c::tr("Recovery directory") + ": " +
                     r.at("recovery_directory").get<std::string>());
            }
            if (ImGui::TreeNode(c::tr("LFS file states").c_str())) {
                const std::map<std::string, const char *> states{
                    {"pointer", "Committed pointer"},
                    {"materialized", git_dehydrate ? "Asset to preserve" : "Already materialized"},
                    {"modified", "Local modifications preserved"},
                    {"missing", "Local deletion preserved"},
                    {"staged", "Staged changes preserved"},
                    {"index_flags", "Special index flags preserved"},
                    {"read_only", "Read-only file preserved"},
                    {"attributes", "LFS attributes required"},
                    {"blocked", "Materialization blocked"}};
                ImGui::BeginChild("lfs-file-list", c::dp(0, 160), ImGuiChildFlags_Borders);
                for (const auto &file : r.at("files")) {
                    text(file.at("path").get<std::string>() + " · " +
                         c::tr(states.at(file.at("state"))));
                    if (file.at("cache") == "unavailable")
                        text(c::tr("LFS cache object unavailable"));
                }
                ImGui::EndChild();
                ImGui::TreePop();
            }
            for (const auto &issue : r.at("errors"))
                ImGui::TextWrapped("%s", (issue.at("path").get<std::string>() + ": " +
                                          issue.at("error").get<std::string>())
                                             .c_str());
            ImGui::BeginDisabled(
                busy() || !r.at(git_dehydrate ? "can_dehydrate" : "can_materialize").get<bool>());
            if (button(git_dehydrate ? "Restore committed LFS pointers"
                                     : "Materialize verified assets",
                       true)) {
                Json input{{"apply", true},
                           {"plan", r.at("plan")},
                           {"base", r.at("base")},
                           {"local_ref", r.at("local_ref")}};
                input["operation"] = r.at("operation");
                if (!r.at("selection").is_null())
                    input["paths"] = r.at("selection");
                git_materialize_state = nullptr;
                action("git-materialize", input,
                       [this, paths_text = git_materialization_paths,
                        inverse = git_dehydrate](const Json &result) {
                           if (result.at("project") == selected &&
                               paths_text == git_materialization_paths && inverse == git_dehydrate)
                               git_materialize_state = result;
                       });
            }
            ImGui::EndDisabled();
        }
        c::endCard();
    }
    void git_receipts_view() {
        c::beginCard("git-integration-receipts", c::tr("Git integration receipts").c_str());
        ImGui::TextWrapped("%s", c::tr("Check the repository before marking an interruption as "
                                       "reviewed. Receipts and Git files are preserved.")
                                     .c_str());
        auto load = [this](size_t offset) {
            git_integration_check = nullptr;
            action("git-integrations", {{"offset", offset}}, [this, offset](const Json &r) {
                git_integration_records = r;
                git_integration_offset = offset;
            });
        };
        if (button("Load integration receipts"))
            load(0);
        if (!git_integration_records.is_null()) {
            ImGui::BeginChild("integration-receipt-list", c::dp(0, 320), ImGuiChildFlags_Borders);
            if (git_integration_records.at("records").empty())
                text(c::tr("No integration receipts"));
            for (const auto &record : git_integration_records.at("records")) {
                auto id = record.at("id").get<std::string>();
                ImGui::PushID(id.c_str());
                text(record.at("created").get<std::string>() + " / " +
                     record.at("peer").get<std::string>() + " / " +
                     record.at("branch").get<std::string>());
                text(id);
                if (record.contains("result_commit") && record.at("result_commit").is_string())
                    text(c::tr("Merge commit") + ": " +
                         record.at("result_commit").get<std::string>());
                if (record.contains("resolved_conflicts"))
                    text(c::tr("Resolved conflicts") + ": " +
                         record.at("resolved_conflicts").dump());
                if (record.contains("edited_files"))
                    text(c::tr("Edited files") + ": " + record.at("edited_files").dump());
                text(c::tr(record.at("status") == "complete" ? "Integration complete"
                                                             : "Integration incomplete"));
                if (record.contains("acknowledged_at"))
                    text(c::tr("Recovery reviewed") + " / " +
                         record.at("acknowledged_at").get<std::string>());
                text(record.at("receipt_path"));
                if (button("Check integration receipt")) {
                    git_integration_check = nullptr;
                    action("git-integration-check", {{"receipt", id}},
                           [this](const Json &r) { git_integration_check = r; });
                }
                if (!git_integration_check.is_null() && git_integration_check.at("receipt") == id) {
                    const auto &checked = git_integration_check;
                    auto position = checked.at("position").get<std::string>();
                    text(c::tr(position == "after" ? "Repository at integrated commit"
                               : position == "before"
                                   ? "Repository at original commit"
                                   : "Repository state differs or is unavailable"));
                    if (checked.at("tracked_matches").get<bool>())
                        text(c::tr("Tracked files and index verified"));
                    if (checked.at("local_data_matches").is_null())
                        text(c::tr("Local data verification unavailable"));
                    else if (checked.at("local_data_matches").get<bool>())
                        text(c::tr("Local data metadata verified"));
                    for (const auto &issue : checked.at("errors"))
                        text(c::tr(issue.get_ref<const std::string &>().c_str()));
                    ImGui::BeginDisabled(!checked.value("can_acknowledge", false));
                    if (button("Mark as reviewed")) {
                        auto query = Json{{"receipt", id}, {"plan", checked.at("plan")}};
                        git_integration_check = nullptr;
                        action("git-integration-acknowledge", query, [this](const Json &r) {
                            if (!git_integration_records.is_null())
                                for (auto &item : git_integration_records.at("records"))
                                    if (item.at("id") == r.at("id"))
                                        item = r;
                        });
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            if (git_integration_records.at("truncated").get<bool>())
                text(c::tr(
                    "Receipt scan limit reached; inspect the state directory for older records."));
            for (const auto &issue : git_integration_records.at("errors"))
                text(issue.dump());
            if (git_integration_offset > 0 && button("Previous receipts"))
                load(git_integration_offset >= 20 ? git_integration_offset - 20 : 0);
            if (git_integration_records.at("has_more").get<bool>() && button("Next receipts"))
                load(git_integration_offset + 20);
            ImGui::EndChild();
        }
        c::endCard();
    }
    void cyrevision_request(const std::string &operation, Json data = Json::object()) {
        auto id = selected;
        submit([this, id, operation, data] {
            return core.request(operation == "status" || operation == "inbox" ? "GET" : "POST",
                                "/api/projects/" + id + "/cyrevision-" + operation, data);
        }, [this, id, operation](const Json &result) {
            if (id != selected) return;
            output = result.dump(2);
            if (operation == "inbox") cyrevision_incoming = result;
            else if (operation == "recover") cyrevision_recovery = result;
            else if (operation == "status" || operation == "configure") {
                cyrevision_state = result;
                cyrevision_guid = result.value("project_id", "");
                cyrevision_legacy_windows = result.value("signature_format", "lf") == "windows-legacy";
            } else if (operation == "import") { git_branches = nullptr; git_loaded = false; }
            if (operation == "trust" || operation == "revoke" || operation == "export") cyrevision_incoming = nullptr;
        });
    }
    void cyrevision_view() {
        if (!ImGui::CollapsingHeader(c::tr("CyRevision exchange").c_str())) return;
        if (button("Help")) open_help("CyRevision exchange");
        ImGui::BeginDisabled(busy());
        if (c::beginProperties("cyrevision-settings")) {
            field("Publishing name", cyrevision_name);
            field("CyRevision project GUID", cyrevision_guid);
            c::endProperties();
        }
        if (button("Create or show public identity")) {
            auto name = cyrevision_name;
            submit([this, name] { return core.request(name.empty() ? "GET" : "POST", "/api/cyrevision/identity",
                name.empty() ? Json::object() : Json{{"name", name}}); }, [this](const Json &result) { output = result.dump(2); });
        }
        ImGui::Checkbox(c::trLabel("Legacy Windows signatures").c_str(), &cyrevision_legacy_windows);
        ImGui::TextWrapped("%s", c::tr("Use legacy signatures for an unmodified Windows CyRevision receiver. Updated receivers accept LF and legacy Windows signatures.").c_str());
        if (button("Configure CyRevision exchange")) {
            cyrevision_incoming = cyrevision_recovery = nullptr;
            cyrevision_request("configure", {{"project_id", cyrevision_guid}, {"signature_format", cyrevision_legacy_windows ? "windows-legacy" : "lf"}});
        }
        ImGui::SameLine();
        if (button("Load CyRevision settings")) cyrevision_request("status");
        ImGui::TextWrapped("%s", c::tr("Verify the key fingerprint through a separate trusted channel. Private signing keys stay in the profile.").c_str());
        ImGui::TextUnformatted(c::tr("Public identity JSON").c_str());
        ImGui::InputTextMultiline("##cyrevision-identity", &cyrevision_public_json, ImVec2(-1, 100));
        if (c::beginProperties("cyrevision-pin")) {
            field("Verified key fingerprint", cyrevision_fingerprint);
            if (ImGui::InputText(c::trLabel("Publishing device GUID").c_str(), &cyrevision_device)) cyrevision_recovery = nullptr;
            c::endProperties();
        }
        if (button("Trust publishing device")) {
            try {
                auto identity = Json::parse(cyrevision_public_json);
                if (identity.contains("identity")) identity = identity.at("identity");
                cyrevision_request("trust", {{"identity", identity}, {"fingerprint", cyrevision_fingerprint}});
            } catch (const std::exception &e) { error = e.what(); }
        }
        ImGui::SameLine();
        if (button("Revoke publishing device")) cyrevision_request("revoke", {{"device", cyrevision_device}});
        if (button("Export to CyRevision", true)) cyrevision_request("export");
        ImGui::SameLine();
        if (button("Refresh CyRevision publications")) cyrevision_request("inbox");
        if (!cyrevision_incoming.is_null()) {
            for (const auto &publication : cyrevision_incoming.at("publications")) {
                auto transaction = publication.at("transactionId").get<std::string>();
                ImGui::PushID(transaction.c_str());
                text(publication.at("author").at("displayName").get<std::string>() + " / " + publication.at("createdAt").get<std::string>());
                text(transaction);
                if (button("Import signed publication")) cyrevision_request("import", {{"transaction", transaction}});
                ImGui::PopID();
            }
            if (!cyrevision_incoming.at("rejected").empty()) text(cyrevision_incoming.at("rejected").dump(2));
        }
        if (button("Preview interrupted import recovery")) cyrevision_request("recover", {{"device", cyrevision_device}});
        if (!cyrevision_recovery.is_null()) {
            text(cyrevision_recovery.dump(2));
            if (cyrevision_recovery.value("can_recover", false) && !cyrevision_recovery.value("applied", false) && button("Apply import recovery"))
                cyrevision_request("recover", {{"device", cyrevision_device}, {"apply", true}, {"plan", cyrevision_recovery.at("plan")}});
        }
        ImGui::EndDisabled();
    }
    void git_view() {
        if (project()["mode"] != "git-bundles") {
            c::callout(c::Tone::info, c::tr("Use Git exchange mode for this view.").c_str());
            return;
        }
        if (button("Refresh")) {
            git_merge_state = git_merge_file_state = nullptr;
            git_integration_records = git_integration_check = nullptr;
            git_integration_offset = 0;
            git_integrate_state = git_integrate_input = nullptr;
            clear_git_merge_choices();
            git_branches = git_review_state = git_review_diff = git_history_state =
                git_commit_state = git_setup_state = nullptr;
            git_setup_requested = false;
            git_loaded = git_storage_current = false;
        }
        git_receipts_view();
        git_materialization_receipts_view();
        auto setupQuery = Json{{"branch", git_initial_branch},
                               {"object_format", git_object_format ? "sha256" : "sha1"}};
        if (git_setup_state.is_null()) {
            if (!git_setup_requested && !busy()) {
                git_setup_requested = true;
                action("git-setup", setupQuery, [this](const Json &r) { git_setup_state = r; });
            }
            return;
        }
        if (git_setup_state.value("can_initialize", false)) {
            c::beginCard("git-setup", c::tr("Repository setup").c_str());
            text(git_setup_state.at("root"));
            if (c::beginProperties("git-setup-properties")) {
                field("Initial branch", git_initial_branch);
                c::property(c::tr("Git object format").c_str());
                ImGui::Combo("##git-object-format", &git_object_format, "SHA-1\0SHA-256\0");
                c::endProperties();
            }
            ImGui::TextWrapped("%s", c::tr("Initialization creates Git metadata and preserves "
                                           "existing files. It does not check out peer files.")
                                         .c_str());
            auto input = Json{{"branch", git_initial_branch},
                              {"object_format", git_object_format ? "sha256" : "sha1"}};
            if (button("Preview Git setup"))
                action("git-setup", input, [this](const Json &r) { git_setup_state = r; });
            ImGui::SameLine();
            ImGui::BeginDisabled(git_setup_state.at("branch") != input.at("branch") ||
                                 git_setup_state.at("object_format") != input.at("object_format"));
            if (button("Initialize repository", true)) {
                input["apply"] = true;
                input["plan"] = git_setup_state.at("plan");
                action("git-setup", input, [this](const Json &r) {
                    git_setup_state = r;
                    git_loaded = false;
                    git_branches = nullptr;
                });
            }
            ImGui::EndDisabled();
            c::endCard();
            return;
        }
        git_materialize_view();
        cyrevision_view();
        c::callout(
            c::Tone::info,
            c::tr("Git and LFS imports preserve the working tree. Review and merge using Git.")
                .c_str());
        auto load = [this] {
            git_loaded = true;
            action("inbox", Json::object(), [this](const Json &r) { incoming = r; });
        };
        if (!git_loaded && !busy())
            load();
        if (git_branches.is_null() && !busy()) {
            git_branches = Json::array();
            action("git-branches", Json::object(), [this](const Json &r) { git_branches = r; });
        }
        ImGui::Checkbox(c::trLabel("Include Git LFS history").c_str(), &include_git_lfs);
        if (button("Export committed branch", true))
            action("export", {{"lfs", include_git_lfs}}, [this](const Json &) {
                git_loaded = false;
                git_storage_current = false;
            });
        ImGui::SameLine();
        if (button("Inspect Git"))
            action("git-status", Json::object(), [this](const Json &r) { git_state = r; });
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
            if (!git_integrate_state.is_null() && git_integrate_state.value("applied", false)) {
                text(c::tr("Integration complete"));
                text(git_integrate_state.at("receipt_path"));
            }
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
                               git_merge_state = git_merge_file_state = nullptr;
                               git_integrate_state = git_integrate_input = nullptr;
                               clear_git_merge_choices();
                               git_review_diff = git_history_state = git_commit_state = nullptr;
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
                                                   {"unrelated", "Histories are unrelated"},
                                                   {"unborn", "No local commit yet"}};
            c::callout(c::Tone::info,
                       c::tr(labels.at(git_review_state.at("relation")).c_str()).c_str());
            text(c::tr("Local commits") + ": " + git_review_state.at("local_only_commits").dump() +
                 " / " + c::tr("Peer commits") + ": " +
                 git_review_state.at("peer_only_commits").dump());
            if (button("Load branch history"))
                load_git_history();
            if (!git_history_state.is_null()) {
                c::heading(c::tr("Branch history").c_str(), 3);
                auto offset = git_history_state.at("offset").get<uintmax_t>();
                auto limit = git_history_state.at("limit").get<uintmax_t>();
                ImGui::BeginDisabled(offset == 0);
                if (button("Previous page"))
                    load_git_history(offset > limit ? offset - limit : 0);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(git_history_state.at("next_offset").is_null());
                if (button("Next page"))
                    load_git_history(git_history_state.at("next_offset"));
                ImGui::EndDisabled();
                if (git_history_state.at("truncated"))
                    text(c::tr("History page limit reached"));
                ImGui::BeginChild(
                    "git-history-list",
                    ImVec2(0,
                           std::min(280.f,
                                    std::max(90.f, git_history_state.at("commits").size() * 78.f))),
                    ImGuiChildFlags_Borders);
                for (const auto &entry : git_history_state.at("commits")) {
                    auto oid = entry.at("oid").get<std::string>();
                    ImGui::PushID(oid.c_str());
                    text(oid.substr(0, 12) + " / " + entry.at("subject").get<std::string>());
                    text(entry.at("author").at("name").get<std::string>() + " / " +
                         entry.at("author").at("date").get<std::string>());
                    if (button("Review commit"))
                        load_git_commit(oid);
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
            if (!git_commit_state.is_null()) {
                const auto &entry = git_commit_state.at("commit");
                text(entry.at("oid").get<std::string>() + " / " +
                     entry.at("subject").get<std::string>());
                for (auto key : {"author", "committer"})
                    text(c::tr(key == std::string("author") ? "Author" : "Committer") + ": " +
                         entry.at(key).at("name").get<std::string>() + " <" +
                         entry.at(key).at("email").get<std::string>() + ">");
                if (entry.at("parents").empty())
                    text(c::tr("Root commit"));
                for (size_t i = 0; i < entry.at("parents").size(); ++i) {
                    auto label = c::tr("Compare with parent") + " " + std::to_string(i + 1);
                    if (c::button(label.c_str()))
                        load_git_commit(entry.at("oid"), static_cast<unsigned>(i + 1));
                    ImGui::SameLine();
                    text(entry.at("parents")[i]);
                }
                if (button("Return to branch comparison"))
                    git_commit_state = git_review_diff = nullptr;
            }
            const auto &comparison =
                git_commit_state.is_null() ? git_review_state : git_commit_state;
            if (git_commit_state.is_null()) {
                git_merge_view();
                bool merging = git_review_state.at("relation") == "diverged";
                auto integration_action = merging ? "git-merge" : "git-integrate";
                c::beginCard(
                    "git-integrate",
                    c::tr(merging ? "Merge diverged branch" : "Integrate imported branch").c_str());
                ImGui::TextWrapped(
                    "%s",
                    c::tr(
                        merging
                            ? "Creates a reviewed two-parent merge commit. Choose committed files "
                              "or edit conflict text, then preview again. Unrelated local data is "
                              "preserved."
                            : "Updates the local branch, index and tracked files. Unrelated local "
                              "files are preserved. LFS pointers require explicit selection.")
                        .c_str());
                auto integrateQuery = git_context();
                if (!merging) {
                    ImGui::BeginDisabled(busy());
                    if (ImGui::Checkbox(c::tr("Keep LFS pointers during fast-forward").c_str(),
                                        &git_integrate_lfs_pointers))
                        git_integrate_state = git_integrate_input = nullptr;
                    ImGui::EndDisabled();
                    if (git_integrate_lfs_pointers) {
                        integrateQuery["lfs_policy"] = "pointers";
                        ImGui::TextWrapped("%s",
                                           c::tr("LFS assets are not expanded. Materialize them "
                                                 "separately after integration. Modified or "
                                                 "expanded working files block this operation.")
                                               .c_str());
                    }
                }
                if (merging && !git_merge_choices.empty()) {
                    integrateQuery["resolutions"] = Json::array();
                    for (const auto &[path, side] : git_merge_choices) {
                        Json entry = {{"path", path}, {"side", side}};
                        if (side == "edited")
                            entry["content"] = git_merge_edits.at(path);
                        integrateQuery["resolutions"].push_back(entry);
                    }
                }
                text(git_review_state.at("head"));
                ImGui::BeginDisabled(busy() || !git_merge_editor.is_null());
                if (button(merging ? "Preview repository merge" : "Preview integration")) {
                    // Keep conflict controls available when edited text fails validation.
                    git_integrate_input = nullptr;
                    action(
                        integration_action, integrateQuery, [this, integrateQuery](const Json &r) {
                            if (current_git_context(r)) {
                                auto controls = git_integrate_state.is_null()
                                                    ? Json::array()
                                                    : git_integrate_state.value("conflict_files",
                                                                                Json::array());
                                git_integrate_state = r;
                                if (!r.value("can_integrate", false) &&
                                    r.contains("conflict_files") &&
                                    r.at("conflict_files").empty() && !controls.empty()) {
                                    git_integrate_state["conflict_files"] = controls;
                                    git_integrate_state["conflicts"] = Json::array();
                                    for (const auto &file : controls)
                                        git_integrate_state["conflicts"].push_back(file.at("path"));
                                }
                                git_integrate_input = integrateQuery;
                            }
                        });
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(busy() || !git_merge_editor.is_null() ||
                                     git_integrate_state.is_null() ||
                                     git_integrate_input != integrateQuery ||
                                     !git_integrate_state.value("can_integrate", false) ||
                                     git_integrate_state.value("applied", false));
                if (button(merging ? "Apply reviewed merge" : "Apply fast-forward", true)) {
                    integrateQuery.update(
                        {{"apply", true}, {"plan", git_integrate_state.at("plan")}});
                    git_integrate_input = nullptr;
                    action(integration_action, integrateQuery, [this](const Json &r) {
                        git_integrate_state = r;
                        git_review_state = git_review_diff = git_history_state = git_commit_state =
                            nullptr;
                        git_loaded = git_storage_current = false;
                    });
                }
                ImGui::EndDisabled();
                if (merging && !git_integrate_state.is_null() &&
                    git_integrate_input != integrateQuery)
                    text(c::tr("Choices changed; preview again"));
                if (merging && !git_integrate_state.is_null() &&
                    git_integrate_state.value("integration_kind", std::string("fast_forward")) ==
                        "merge") {
                    const auto &merge = git_integrate_state;
                    if (merge.at("result_tree").is_string())
                        text(c::tr("Reviewed result tree") + ": " +
                             merge.at("result_tree").get<std::string>());
                    if (merge.at("merge_identity").is_object())
                        text(c::tr("Merge author") + ": " +
                             merge.at("merge_identity").at("name").get<std::string>() + " <" +
                             merge.at("merge_identity").at("email").get<std::string>() + ">");
                    if (!merge.at("conflicts").empty()) {
                        text(c::tr("Repository merge conflicts"));
                        ImGui::BeginChild("repository-merge-conflicts", c::dp(0, 160),
                                          ImGuiChildFlags_Borders);
                        text(c::tr("Each choice replaces the entire file, including nonconflicting "
                                   "changes. A missing version deletes it."));
                        for (const auto &file : merge.at("conflict_files")) {
                            auto path = file.at("path").get<std::string>();
                            ImGui::PushID(path.c_str());
                            text(path);
                            auto found = git_merge_choices.find(path);
                            auto selected_side =
                                found == git_merge_choices.end() ? "" : found->second;
                            ImGui::BeginDisabled(busy());
                            for (auto side : {"local", "peer"}) {
                                auto label =
                                    file.at(side).is_null()
                                        ? (std::string(side) == "local" ? "Use local deletion"
                                                                        : "Use peer deletion")
                                        : (std::string(side) == "local" ? "Use local file"
                                                                        : "Use peer file");
                                if (ImGui::RadioButton(c::tr(label).c_str(),
                                                       selected_side == side)) {
                                    git_merge_choices[path] = side;
                                    git_integrate_input = nullptr;
                                }
                                ImGui::SameLine();
                            }
                            if (button("Leave unresolved")) {
                                git_merge_choices.erase(path);
                                git_integrate_input = nullptr;
                            }
                            ImGui::BeginDisabled(!file.at("editable").get<bool>() ||
                                                 !git_merge_editor.is_null());
                            if (button("Edit merged text")) {
                                auto input = git_context();
                                input["path"] = path;
                                input["merge_base"] = merge.at("merge_base");
                                action("git-merge-file", input, [this, path](const Json &r) {
                                    if (!current_git_context(r) || git_integrate_state.is_null() ||
                                        r.at("merge_base") != git_integrate_state.at("merge_base"))
                                        return;
                                    git_merge_editor = r;
                                    git_region_choices.assign(
                                        r.at("regions").at("count").get<size_t>(), "");
                                    auto found = git_merge_edits.find(path);
                                    git_merge_buffer =
                                        found != git_merge_edits.end() ? found->second
                                        : r.at("candidate").is_string()
                                            ? r.at("candidate").get<std::string>()
                                            : r.at("texts")
                                                  .at(r.at("local_info").at("exists").get<bool>()
                                                          ? "local"
                                                          : "peer")
                                                  .get<std::string>();
                                });
                            }
                            ImGui::EndDisabled();
                            if (!file.at("editable").get<bool>() &&
                                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                ImGui::SetTooltip(
                                    "%s", c::tr("Text editing requires matching file permissions")
                                              .c_str());
                            if (selected_side == "edited")
                                text(c::tr("Edited text selected"));
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                }
                git_merge_editor_view();
                if (!git_integrate_state.is_null() &&
                    git_integrate_state.value("lfs_policy", std::string("reject")) == "pointers")
                    c::textDim("%s: %zu", c::tr("LFS pointer files").c_str(),
                               git_integrate_state.at("lfs_after").at("files").get<size_t>());
                if (!git_integrate_state.is_null() &&
                    !git_integrate_state.at("local_data").is_null()) {
                    const auto &local = git_integrate_state.at("local_data");
                    text(c::tr("Local files preserved") + ": " + local.at("files").dump() + " / " +
                         local.at("bytes").dump() + " B");
                }
                if (!git_integrate_state.is_null() && !git_integrate_state.at("errors").empty()) {
                    text(c::tr("Integration blocked"));
                    for (const auto &reason : git_integrate_state.at("errors"))
                        text(c::tr(reason.get<std::string>().c_str()));
                }
                c::endCard();
            }
            c::beginCard("git-extract", c::tr("Extract Git revision").c_str());
            ImGui::TextWrapped("%s", c::tr("Copies the complete selected revision. Local Git files "
                                           "and changes are preserved.")
                                         .c_str());
            ImGui::TextWrapped(
                "%s",
                c::tr(
                    "File snapshot limits: 1 GiB per Git blob, 4 GiB per LFS asset, 16 GiB total.")
                    .c_str());
            if (c::beginProperties("git-extract-properties")) {
                field("New extraction directory", git_extract_destination);
                c::endProperties();
            }
            ImGui::Checkbox(c::trLabel("Expand verified LFS assets").c_str(), &git_extract_lfs);
            auto extractQuery = git_context();
            extractQuery["commit"] = git_commit_state.is_null()
                                         ? git_review_state.at("head")
                                         : git_commit_state.at("commit").at("oid");
            extractQuery["destination"] = git_extract_destination;
            extractQuery["lfs"] = git_extract_lfs;
            text(extractQuery.at("commit"));
            ImGui::BeginDisabled(git_extract_destination.empty());
            if (button("Preview extraction")) {
                git_extract_state = git_extract_input = nullptr;
                action("git-extract", extractQuery, [this, extractQuery](const Json &r) {
                    if (current_git_context(r)) {
                        git_extract_state = r;
                        git_extract_input = extractQuery;
                    }
                });
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(git_extract_state.is_null() || git_extract_input != extractQuery ||
                                 !git_extract_state.value("can_extract", false) ||
                                 git_extract_state.value("applied", false));
            if (button("Extract to new directory", true)) {
                extractQuery.update({{"apply", true}, {"plan", git_extract_state.at("plan")}});
                git_extract_input = nullptr;
                action("git-extract", extractQuery,
                       [this](const Json &r) { git_extract_state = r; });
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (!git_extract_state.is_null()) {
                text(c::tr("Files") + ": " + git_extract_state.at("file_count").dump() + " / " +
                     git_extract_state.at("bytes").dump() +
                     " B / LFS: " + git_extract_state.at("lfs_files").dump());
                if (git_extract_state.value("applied", false)) {
                    text(c::tr("Extraction complete"));
                    text(git_extract_state.at("receipt_path"));
                }
                if (!git_extract_state.at("errors").empty()) {
                    text(c::tr("Extraction blocked"));
                    text(git_extract_state.at("errors").dump(2));
                }
            }
            c::endCard();
            if (comparison.at("files").empty())
                text(c::tr("No changed files"));
            for (const auto &file : comparison.at("files")) {
                auto path = file.at("path").get<std::string>();
                ImGui::PushID(path.c_str());
                text(file.at("status").get<std::string>() + " / " + path);
                if (button("Review file")) {
                    auto query = git_context();
                    query["path"] = path;
                    if (!git_commit_state.is_null()) {
                        query["commit"] = git_commit_state.at("commit").at("oid");
                        query["parent"] = git_commit_state.at("parent");
                    }
                    action(query.contains("commit") ? "git-commit-file" : "git-review-file", query,
                           [this, query](const Json &r) {
                               bool sameCommit =
                                   query.contains("commit")
                                       ? !git_commit_state.is_null() &&
                                             git_commit_state.at("commit").at("oid") ==
                                                 query.at("commit") &&
                                             git_commit_state.at("parent") == query.at("parent")
                                       : git_commit_state.is_null();
                               if (current_git_context(r) && sameCommit)
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
                               git_branches = git_review_state = git_review_diff =
                                   git_history_state = git_commit_state = nullptr;
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
            c::property(c::tr("Backup storage").c_str());
            backup_policy_selector("##backup-storage");
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
        ImGui::TextWrapped(
            "%s", c::tr("Content-defined blocks support files from 8 MiB to 64 GiB and can "
                        "preserve reuse after insertions. Other sizes use whole files.")
                      .c_str());
        if (p.at("mode") == "git-bundles")
            ImGui::TextWrapped("%s",
                               c::tr("Leave empty for the default Git cache. A custom directory "
                                     "must already exist and match Git lfs.storage.")
                                   .c_str());
        if (button("Save", true)) {
            auto id = selected;
            auto data = Json{{"backup_interval_minutes", interval},
                             {"backup_keep", keep},
                             {"backup_storage", backup_policy_value(backup_policy)},
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
        mode_options_view(p);
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
                    if (!args.empty() && (args[0] == "docs" || args[0] == "documentation"))
                        return cysynck::help::command(args);
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
            auto current = modes[mode].contains("plugin")
                               ? plugin_label(modes[mode], "label")
                               : c::tr(modes[mode]["label"].get<std::string>().c_str());
            if (ImGui::BeginCombo("##mode", current.c_str())) {
                for (size_t i = 0; i < modes.size(); ++i)
                    if (ImGui::Selectable(
                            (modes[i].contains("plugin")
                                 ? plugin_label(modes[i], "label")
                                 : c::tr(modes[i]["label"].get<std::string>().c_str()))
                                .c_str(),
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
            c::property(c::tr("Backup storage").c_str());
            backup_policy_selector("##new-backup-storage");
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
                {"backup_storage", backup_policy_value(backup_policy)},
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
        std::string screenshot, view, theme, language, renderer = "auto";
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
            else if (flag == "--renderer")
                renderer = args[i];
            else
                throw std::runtime_error("Unknown desktop option: " + flag);
        }
        if (renderer != "auto" && renderer != "opengl2" && renderer != "opengl3")
            throw std::runtime_error("Renderer must be auto, opengl2 or opengl3");
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
        glfwSetErrorCallback([](int code, const char *description) {
            std::cerr << "GLFW " << code << ": " << (description ? description : "Unknown error") << '\n';
        });
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed; see the graphics diagnostic above");
        const char *glsl = "#version 130";
        bool legacy_renderer = renderer == "opengl2";
        auto create_window = [&](bool legacy) {
            glfwDefaultWindowHints();
            // Keep capture windows hidden; interactive launches show after tray setup.
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, legacy ? 2 : 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, legacy ? 1 : 0);
#ifdef __APPLE__
            if (!legacy) {
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
                glsl = "#version 150";
            }
#endif
            return glfwCreateWindow(1440, 960, "CySynck", nullptr, nullptr);
        };
        window = create_window(legacy_renderer);
        if (!window && renderer == "auto") {
            std::cerr << "Retrying with the OpenGL 2.1 compatibility renderer.\n";
            legacy_renderer = true;
            window = create_window(true);
        }
        if (!window)
            throw std::runtime_error("Cannot create native OpenGL window");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        if (!screenshot.empty())
            std::cout << "Renderer: " << (legacy_renderer ? "opengl2" : "opengl3") << "; "
                      << glGetString(GL_RENDERER) << "; " << glGetString(GL_VERSION) << '\n';
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().IniFilename = nullptr;
        if (!ImGui_ImplGlfw_InitForOpenGL(window, true) ||
            !(legacy_renderer ? ImGui_ImplOpenGL2_Init() : ImGui_ImplOpenGL3_Init(glsl)))
            throw std::runtime_error("Cannot initialize the desktop graphics backend");
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
                if (legacy_renderer)
                    ImGui_ImplOpenGL2_NewFrame();
                else
                    ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();
                app.draw();
                if (ImGui::IsKeyPressed(ImGuiKey_F1, false))
                    app.open_help();
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
                if (legacy_renderer)
                    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
                else
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
        if (legacy_renderer)
            ImGui_ImplOpenGL2_Shutdown();
        else
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
