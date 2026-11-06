# GitHub preparation and publishing

The intended remote is **https://github.com/MrMybal/CySynck**. Keep its current private
visibility unless the owner explicitly decides to change it.

## Repository page

Use the English About description stored in [`.github/description.txt`](../.github/description.txt):

> Project-based sync and backup companion in C++20, powered by Syncthing and CyUI. Native desktop, CLI, Web console, diffs, recovery, plugins and WireGuard integration.

The default landing page is [README.md](../README.md), in English. A French companion is
available as [README.fr.md](../README.fr.md). The core and CLI use English; the desktop
and Web interfaces default to English and offer French as the second language.

Suggested repository topics: `cpp20`, `syncthing`, `file-synchronization`, `backup`,
`desktop-app`, `cli`, `wireguard`, `unreal-engine`.

## Source scope

Publish application source, assets, build definitions, tests, documentation, deployment
scripts, integration examples and GitHub workflows. Third-party source is fetched by CMake
from the references in [THIRD_PARTY.md](../THIRD_PARTY.md).

CySynck's source and examples are distributed under the [MIT License](../LICENSE).
Keep `LICENSE` at the repository root and in installed distributions. Dependency
license notices remain attached to their respective components.

Generated `build*/`, `dist/`, `.local/`, virtual environments, logs, local environment files
and private key files are ignored. In particular, `.local/` contains development tools,
audit reports, private test identities and archived prototypes. Those files do not belong
in the source repository. Binary release ZIPs should be attached to a GitHub Release when
one is intentionally published, rather than committed to Git.

## Gitleaks

Use the official [Gitleaks CLI](https://github.com/gitleaks/gitleaks), currently pinned by
the workflow to **8.30.1**, with its default detection rules. Download the appropriate
official release archive and verify its SHA-256 against the published release checksums.
There is no project-wide test exclusion, baseline or allowlist.

For a clean source directory, including files not yet committed:

```sh
gitleaks dir /path/to/source-export --redact --no-banner --report-format json --report-path /private/path/gitleaks-source.json
```

The source export must contain every file intended for Git, including hidden configuration
files. Do not scan a generated build tree as a substitute for auditing the sources.
For an existing local repository, scan every committed ref as well:

```sh
gitleaks git . --log-opts="--all" --redact --no-banner --report-format json --report-path .local/gitleaks-history.json
```

Exit code `0` means no findings under the selected rules and scope. Exit code `1` means
findings require review; other failures must also be resolved before publishing.
Reports are redacted and kept outside committed sources. A clean result does not certify
the absence of every possible secret or replace reviewing the staged file list.

The [Secret scan workflow](../.github/workflows/secrets.yml) runs the CLI on pushes,
pull requests and manual dispatch. It checks the complete fetched Git history, verifies
the downloaded tool against a pinned checksum, and needs only read access to repository
contents. It uses the CLI directly, so no Gitleaks Action license key is required.

## Connect and inspect the remote

If the source folder is not already a Git repository:

```sh
git init -b main
git remote add origin https://github.com/MrMybal/CySynck.git
```

If it is already initialized, inspect `git remote -v` instead of adding a duplicate remote.
Verify access through the configured Git Credential Manager or an authorized SSH key:

```sh
git ls-remote --symref origin
```

GitHub's connected app and local Git have separate authentication. An app returning `404`
for a private repository does not imply that local Git cannot access it. Do not place
access tokens in remote URLs, source files or shell command arguments.

## First commit and upload

After auditing the complete source export, review the local candidate list:

```sh
git status --short
git ls-files --others --exclude-standard
git add .clang-format .gitattributes .gitignore .github CMakeLists.txt LICENSE README.md README.fr.md THIRD_PARTY.md assets cmake deploy docs examples include integrations scripts src tests
git diff --cached --stat
git diff --cached --check
git commit -m "Initial C++20 CySynck application"
gitleaks git . --log-opts="--all" --redact --no-banner
```

For a verified empty remote, the upload command is:

```sh
git push --dry-run origin main
git push -u origin main
```

The dry run checks the proposed ref update; it does not upload source objects. The following
push publishes the commit to the existing private repository and sets its upstream.
If the remote already contains commits, fetch and review them before choosing an integration
strategy. Do not use a forced push to replace an unknown history.

After upload, inspect **Native builds** and **Secret scan** in the repository's Actions tab.
Cross-platform support is confirmed by the actual job results, not by the workflow's presence.
The native workflow uploads build artifacts; it does not automatically create public releases.

Reference: [GitHub's guide to importing local source](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github).
