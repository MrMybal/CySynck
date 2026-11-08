---
id: diagnostics
title: Diagnostics
description: Inspecter le profil, les outils, les projets et les récupérations en attente.
section: Exploitation
order: 61
sourceRevision: 2
---

# Diagnostics {#diagnostics}

`cysynck doctor` inspecte le profil choisi, les outils disponibles, les dossiers des projets, le mode Git et les récupérations en attente.
Un outil absent est une erreur si une fonction configurée l’exige ; sinon c’est un avertissement facultatif.
Les projets de sauvegarde seule n’exigent pas Syncthing. Une connexion à une instance externe n’exige pas d’exécutable Syncthing local.
Un transport CyRevision configuré exige aussi l’outil de signature OpenSSL externe ;
le sélectionner avec `CYSYNCK_OPENSSL`. L’échange Git natif seul n’en a pas besoin.
L’inspection par défaut n’exécute aucun outil, ne contacte pas Syncthing, ne démarre aucun moteur, ne modifie aucun projet et n’active aucun VPN.
`cysynck doctor --probe true` teste explicitement les versions de Git, Syncthing, WireGuard et OpenSSL et l’API Syncthing configurée.
Chaque version est limitée à cinq secondes, la connexion à un court délai. Les outils d’activation sont repérés sans exécution.
Les rapports omettent clés et sorties brutes des processus.
Le même rapport figure dans **Diagnostics** du desktop et du Web, avec libellés anglais/français. Les messages du cœur et le CLI restent anglais.
API : `GET /api/doctor` pour l’inspection ; `POST /api/doctor` avec `probe_tools` et/ou `probe_engine` booléens pour les tests explicites.
GET ignore ces options même dans un corps. Le rapport JSON contient `ready`, les nombres d’erreurs/avertissements et des contrôles à identifiants stables.
`ready:true` signifie seulement que les contrôles effectués n’ont trouvé aucune erreur bloquante ; il ne certifie ni accessibilité réseau, ni intégrité des sauvegardes, ni traversée VPN. Les options de test sont indiquées séparément.
