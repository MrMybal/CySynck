# CySynck 0.41.0

[Manuel français](Documentation/Documentation.html#/fr/overview) · [English manual](Documentation/Documentation.html#/en/overview)

Cette préversion comprend l’assistant natif et 130 opérations communes au CLI/MCP.
Ouvrir **Assistant** pour connecter un fournisseur. Les modifications de l’application
sont désactivées par défaut ; les autoriser explicitement pour l’instance courante.
Fournir les identifiants des agents/API séparément. Le guide local est
`Documentation/Documentation.html#/fr/assistant`. Sous Windows, lancer
`bin/cysynck-desktop.exe` ; `--mcp-port 0` expose son point d’accès local authentifié
au CLI. Le paquet comprend aussi `Install-CySynck.ps1`.

Ouvrir `Documentation/Documentation.html` dans un navigateur récent. Garder le dossier
`Documentation/` entier, avec `DocData/`. Aucun serveur, CDN, Node.js ou accès Internet n’est nécessaire à la lecture.

Dans le desktop, **Aide (F1)** ouvre le sujet courant. Depuis le CLI :

```sh
cysynck docs --language fr
cysynck docs quickstart --language fr
cysynck docs backups --section verifier-et-restaurer --language fr --print
```

`--print` renvoie l’URL locale sans ouvrir de navigateur, notamment sur un serveur sans interface.
Pour un autre poste, copier et extraire tout `CySynck-Documentation.zip`, puis ouvrir son
`Documentation.html`. L’aide contextuelle Web propose aussi cette archive.

Le cœur C++20, le desktop, le CLI, les ressources Web et les exemples SDK sont sous licence MIT.
Les notices accompagnent le paquet et le manuel. Installer Syncthing, Git et WireGuard séparément si nécessaire.
Commencer par les pages d’installation, du premier projet et des limites avant d’utiliser des données importantes.
