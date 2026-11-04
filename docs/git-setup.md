# Git setup and the first import

CySynck 0.16 can initialize an existing project folder explicitly, then inspect a peer's
imported branch before creating any local commit. The same operations are available in the
CLI, local API, C/C++ SDK, native CyUI app and English/French Web console. Git must be installed.

## Initialize a project folder

Create a `git-bundles` project with the same project ID and shared exchange path as the peer.
The project folder must already exist. Initialization accepts a branch name (default `main`)
and object format (`sha1` by default, or `sha256` to match a SHA-256 peer repository).

```sh
cysynck project add Team /absolute/team --id team --mode git-bundles --exchange-path /absolute/exchange --interval 0
cysynck git setup team --branch main --object-format sha1
# Copy the returned plan, keeping the options identical:
cysynck git setup team --branch main --object-format sha1 --apply true --plan PREVIEW_TOKEN
```

Preview creates no Git metadata. Its plan covers the project ID, canonical root, branch and
format. Application exclusively reserves a new `.git` directory, runs Git with empty templates
and disabled hooks/network protocols, then checks the repository identity and unborn HEAD.
It creates no commit, staging entry or peer checkout. Existing working files are preserved.
Repository-specific `GIT_*` environment overrides must be unset, so an inherited Git directory,
index or object-storage redirection cannot redirect setup away from the reviewed folder.

Existing `.git` entries (including worktree files, links or invalid metadata) and bare repositories
are preserved; initialization refuses to reinitialize them. Metadata appearing after preview
also prevents application. If initialization fails after creating metadata, that metadata stays
in place for inspection and the error explains this; automatic repair or deletion is not attempted.

In the desktop Git exchange view and Web Git panel, choose the initial branch and format,
preview the setup, then select **Initialize repository**. Changing the options requires a new
preview. Existing repositories go directly to the exchange controls.

## Inspect the first peer import

```sh
cysynck git inbox team
cysynck git import team PEER BUNDLE
cysynck git branches team
cysynck git review team PEER main
cysynck git history team PEER main --limit 50
cysynck git review-file team PEER main relative/file.cpp
```

Import verifies the bundle and any LFS inventory, then publishes a remote tracking ref. It does
not populate working files. With no local commit yet, branch review reports `relation:"unborn"`
and `base:null`. Its comparison includes the peer's entire current tree against an empty tree,
including files unchanged in the latest commit. History and root/merge commit reviews retain
their usual parent comparison semantics. Dirty files and the staging index stay intact.

Version 0.17 also provides [revision extraction](git-extraction.md): copy an imported tree and
its verified LFS assets into a separate new folder after preview, without changing this repository.

The response's `local_ref` records the symbolic HEAD target. Send it together with `base` and
`head` for stable continuation or file requests. In the CLI, `--base unborn` represents JSON
null and `--local-ref refs/heads/main` pins the local branch. Switching that branch, creating
its first commit or updating the peer ref invalidates pinned requests. Corrupt HEAD targets
fail explicitly. `git status team` reports `unborn`, nullable `head`, `branch` and `local_ref`.

Checking out files, creating a local commit or merging the imported branch remains an explicit
Git operation. Review and import preserve existing work and do not apply the peer's changes.

## API and SDK

`POST /api/projects/ID/git-setup` takes `{"branch":"main","object_format":"sha1"}` for preview.
Application adds `"apply":true` and the returned `"plan"`. The response includes `root`, `branch`,
`object_format`, `metadata_present`, `can_initialize`, `applied`, `plan` and an English `reason`.
An existing `.git` entry disables setup; its presence alone does not validate the repository.

The established token-protected API and UTF-8 JSON C ABI expose this route without a new ABI
version. Read the [branch review](git-review.md) and [history](git-history.md) guides for bounds
and continuation fields. Git's documented [symbolic refs](https://git-scm.com/docs/git-symbolic-ref)
and [revision verification](https://git-scm.com/docs/git-rev-parse) underpin HEAD classification.
