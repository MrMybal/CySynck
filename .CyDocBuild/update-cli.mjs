import { readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const here = dirname(fileURLToPath(import.meta.url));
const source = await readFile(join(here, '../src/commands.cpp'), 'utf8');
const syntax = source.match(/\{"help", R"\(([\s\S]*?)\)"\}\}/)?.[1];
if (!syntax) throw new Error('Cannot find the canonical CLI help literal');
for (const locale of ['en', 'fr']) {
  const english = locale === 'en';
  const title = english ? 'Command-line reference' : 'Référence de la ligne de commande';
  const description = english ? 'Canonical command syntax, profiles and remote API access.' : 'Syntaxe des commandes, profils et accès distant à l’API.';
  const body = english ? `
# Command-line reference {#cli}

## Run a command {#commands}

The core and CLI use English identifiers. The block below is the application's actual help output.
Replace uppercase placeholders, quote paths with spaces and use the guides for each preview/apply workflow.
The standalone executable and the native command panel share Core commands and the local \`docs\`/\`desktop startup\` launchers.
The launchers are not remote Core commands. The desktop Help button also opens the current topic.

\`\`\`text
${syntax}
\`\`\`

## Profiles and remote access {#profiles}

\`--state DIR\` selects a private profile. Defaults: Windows \`%LOCALAPPDATA%/CySynck\`;
macOS \`~/Library/Application Support/CySynck\`; Linux \`$XDG_STATE_HOME/cysynck\` or \`~/.local/state/cysynck\`.
Keep the profile outside synchronized project/exchange folders. Scheduled backups require an active desktop or normal \`serve\` process;
\`--interval 0\` disables the schedule and pausing suspends it.

\`\`\`sh
cysynck --state /absolute/profile serve --host 127.0.0.1 --port 8484
cysynck --state /absolute/profile api-token
cysynck --server http://127.0.0.1:8484 project list
\`\`\`

Use an SSH tunnel for a remote server. Set \`CYSYNCK_API_TOKEN\` in the client environment from the server's private token.
Never put it in synchronized files. Engine setup and tunnel activation remain local-only.
JSON reports can contain \`ok:false\`, \`can_integrate:false\` or \`delivered:false\` without a transport error;
scripts must inspect the operation-specific fields. See [API contracts](doc:architecture) and [offline help](doc:help).
` : `
# Référence de la ligne de commande {#cli}

## Exécuter une commande {#commands}

Le cœur et le CLI emploient des identifiants anglais. Le bloc suivant est la sortie d’aide réelle de l’application.
Remplacer les paramètres en majuscules, citer les chemins avec espaces et consulter chaque guide pour les étapes aperçu/application.
L’exécutable et le panneau de commandes natif partagent les commandes du Core et les lanceurs locaux \`docs\`/\`desktop startup\`.
Ces lanceurs ne sont pas des commandes Core distantes. Le bouton Aide du desktop ouvre aussi le sujet courant.

\`\`\`text
${syntax}
\`\`\`

## Profils et accès distant {#profiles}

\`--state DIR\` choisit un profil privé. Défauts : Windows \`%LOCALAPPDATA%/CySynck\` ;
macOS \`~/Library/Application Support/CySynck\` ; Linux \`$XDG_STATE_HOME/cysynck\` ou \`~/.local/state/cysynck\`.
Garder le profil hors des projets/échanges synchronisés. Les sauvegardes planifiées exigent un desktop ou processus \`serve\` normal actif ;
\`--interval 0\` désactive la planification et la pause la suspend.

\`\`\`sh
cysynck --state /chemin/absolu/profil serve --host 127.0.0.1 --port 8484
cysynck --state /chemin/absolu/profil api-token
cysynck --server http://127.0.0.1:8484 project list
\`\`\`

Utiliser un tunnel SSH pour un serveur distant. Définir \`CYSYNCK_API_TOKEN\` dans l’environnement client avec le jeton privé du serveur.
Ne jamais le synchroniser. Configuration du moteur et activation du tunnel restent locales.
Un rapport JSON peut contenir \`ok:false\`, \`can_integrate:false\` ou \`delivered:false\` sans erreur de transport ;
les scripts doivent lire les champs propres à l’opération. Voir [les contrats API](doc:architecture) et [l’aide hors ligne](doc:help).
`;
  await writeFile(join(here, 'manual', locale, 'cli.md'), `---\nid: cli\ntitle: ${title}\ndescription: ${description}\nsection: ${english ? 'Reference' : 'Référence'}\norder: 80\n${english ? 'revision' : 'sourceRevision'}: 1\n---\n${body}`);
}
console.log('Updated both CLI reference pages from the English command literal');
