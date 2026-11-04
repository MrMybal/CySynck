"use strict";
// English is the source language. Keep API identifiers and user content unchanged.
globalThis.CySynckI18n = (() => {
  const french = {
    "PROJECTS · SYNC · BACKUP": "PROJETS · SYNC · BACKUP",
    "Language": "Langue",
    "Service console": "Console du service",
    "Local access key": "Clé d'accès locale",
    "Connect": "Se connecter",
    "Run": "Exécutez",
    "to display this service's access key.": "pour afficher la clé d'accès de ce service.",
    "Refresh": "Actualiser",
    "WORKSPACE": "ESPACE DE TRAVAIL",
    "Overview": "Vue d'ensemble",
    "Add a project": "Ajouter un projet",
    "Project name": "Nom du projet",
    "Folder path on the server": "Chemin du dossier sur le serveur",
    "Two-way synchronization": "Synchronisation bidirectionnelle",
    "Send only": "Envoi seul",
    "Receive only": "Réception seule",
    "Backup only": "Sauvegarde seule",
    "Git exchange": "Échange Git",
    "Separate exchange folder (Git)": "Dossier d'échange séparé (Git)",
    "Create project": "Créer le projet",
    "Back up": "Sauvegarder",
    "Verify latest backup": "Vérifier la dernière sauvegarde",
    "Compare with latest backup": "Comparer à la dernière sauvegarde",
    "Find conflicts": "Rechercher les conflits",
    "Add to Syncthing": "Ajouter à Syncthing",
    "Synchronize": "Synchroniser",
    "Engine status": "État du moteur",
    "Synchronize requests a scan. Syncthing continues to manage background transfers.": "Synchroniser demande une analyse. Syncthing continue de gérer les transferts en arrière-plan.",
    "Conflict copies": "Copies de conflit",
    "Differences": "Différences",
    "Left relative path": "Chemin relatif à gauche",
    "Right relative path": "Chemin relatif à droite",
    "Compare files": "Comparer les fichiers",
    "Copy patch": "Copier le patch",
    "Select a file from the changes list.": "Sélectionnez un fichier dans la liste des différences.",
    "Backups": "Sauvegardes",
    "Resolve conflict": "Résoudre le conflit",
    "Both source versions are preserved. Pause the project before applying a choice, then resume synchronization explicitly after checking the result.": "Les deux versions sources sont conservées. Mettez le projet en pause avant d'appliquer un choix, puis reprenez explicitement la synchronisation après vérification.",
    "Keep current version": "Garder la version courante",
    "Keep deletion": "Conserver la suppression",
    "Use conflict copy": "Utiliser la copie de conflit",
    "Manual merge": "Fusion manuelle",
    "Manual merge content": "Contenu de la fusion manuelle",
    "UTF-8 editor, LF line endings, maximum 512 KiB.": "Éditeur UTF-8, fins de ligne LF, maximum 512 Kio.",
    "Pause project": "Mettre le projet en pause",
    "Prepare resolution": "Préparer la résolution",
    "Apply resolution": "Appliquer la résolution",
    "Recovery history": "Historique des résolutions",
    "Refresh history": "Actualiser l'historique",
    "Backup storage": "Stockage des sauvegardes",
    "Cleanup removes only objects unused by all retained backups.": "Le nettoyage supprime uniquement les objets inutilisés par toutes les sauvegardes conservées.",
    "Inspect storage": "Analyser le stockage",
    "Free unused space": "Libérer l'espace inutilisé",
    "Recent activity": "Activité récente",
    "No differences": "Aucune différence",
    "Operation refused": "Opération refusée",
    "{projects} projects · {backups} with a backup": "{projects} projets · {backups} avec une sauvegarde",
    "{created} · {count} files · {id}": "{created} · {count} fichiers · {id}",
    "No conflict copies found.": "Aucune copie de conflit trouvée.",
    "The merge exceeds 512 KiB.": "La fusion dépasse 512 Kio.",
    "Result: {bytes} bytes": "Résultat : {bytes} octets",
    "SHA-256: {hash}": "SHA-256 : {hash}",
    "Deletion kept": "Suppression conservée",
    "Recovery copies: approximately {bytes} bytes": "Copies de récupération : environ {bytes} octets",
    "Not enough free space.": "Espace libre insuffisant.",
    "Resolution applied. Synchronization is paused.": "Résolution appliquée. Synchronisation en pause.",
    "Needs recovery": "Récupération à examiner",
    "Prepared": "Préparé",
    "Preparing": "Préparation",
    "Moving current file": "Déplacement du fichier courant",
    "Moving conflict copy": "Déplacement de la copie de conflit",
    "Publishing result": "Publication du résultat",
    "Aborted": "Abandonné",
    "Applied": "Appliqué",
    "Interrupted": "Interrompu",
    "Rolled back": "Annulé",
    "Reviewed": "Examiné",
    "New separate folder on the server": "Nouveau dossier séparé sur le serveur",
    "Recovery copies destination": "Destination des copies de récupération",
    "Export copies": "Exporter les copies",
    "After restoring or checking the working files, mark this recovery as reviewed. The copies are kept.": "Après avoir restauré ou vérifié les fichiers de travail, marquez cette récupération comme examinée. Les copies sont conservées.",
    "Mark as reviewed": "Marquer comme examinée",
    "{snapshots} backups · {bytes} bytes stored": "{snapshots} sauvegardes · {bytes} octets stockés",
    "{objects} unused objects · {bytes} reclaimable bytes": "{objects} objets inutilisés · {bytes} octets récupérables",
    "{objects} missing objects": "{objects} objets manquants",
    "Cleanup in progress…": "Nettoyage en cours…",
    "{objects} objects removed · {bytes} bytes freed": "{objects} objets supprimés · {bytes} octets libérés",
    "Inspect storage again before retrying.": "Relancez l'analyse du stockage avant de réessayer."
  };
  let language = "en";
  function t(key, values = {}) {
    const template = language === "fr" ? french[key] || key : key;
    return template.replace(/\{([a-z_]+)\}/g, (match, name) =>
      Object.hasOwn(values, name) ? String(values[name]) : match);
  }
  function setText(node, key, values = {}) {
    node.dataset.i18n = key;
    node.dataset.i18nParams = JSON.stringify(values);
    node.textContent = t(key, values);
    return node;
  }
  function apply(root = document) {
    for (const node of root.querySelectorAll("[data-i18n]")) {
      const key = node.dataset.i18n || node.textContent;
      setText(node, key, JSON.parse(node.dataset.i18nParams || "{}"));
    }
    for (const [marker, attribute] of [["data-i18n-placeholder", "placeholder"], ["data-i18n-aria", "aria-label"]]) {
      for (const node of root.querySelectorAll("[" + marker + "]")) {
        const key = node.getAttribute(marker) || node.getAttribute(attribute);
        node.setAttribute(marker, key);
        node.setAttribute(attribute, t(key));
      }
    }
  }
  function setLanguage(value) {
    language = value === "fr" ? "fr" : "en";
    document.documentElement.lang = language;
    document.getElementById("language").value = language;
    try { localStorage.setItem("cysynck-language", language); } catch (_) { /* Storage may be disabled. */ }
    apply();
  }
  function init() {
    let saved = "en";
    try { saved = localStorage.getItem("cysynck-language") || "en"; } catch (_) { /* Use English. */ }
    setLanguage(saved);
    document.getElementById("language").addEventListener("change", event => setLanguage(event.target.value));
  }
  return Object.freeze({t, setText, apply, setLanguage, init});
})();
