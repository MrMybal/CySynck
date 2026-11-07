---
id: git-object-inspection
title: Bounded Git object inspection
description: Usage, commands and limits for Bounded Git object inspection.
section: Git
order: 44
parent: git-review
revision: 1
---

# Bounded Git object inspection {#bounded-git-object-inspection}

## Raw working files (0.35) {#raw-working-files-035}

Integration previews, post-apply checks and integration receipt verification use
`git hash-object --no-filters -- <absolute-file>...` in bounded batches. Every selected file
is read and compared with its expected Git blob identity; no timestamp cache replaces the
content check. The existing CLI, C/C++ ABI and CyUI/Web workflows use the same implementation.

Each invocation accepts at most 128 paths, 12,000 UTF-8 bytes of file arguments (with quoting
margin), and 32 MiB of file content. A larger permitted file is hashed alone. Existing limits
of 1 GiB per file, 16 GiB total and 10,000 files still apply. Paths are separate process
arguments, so POSIX filenames containing newlines do not become extra input records. Empty
inventories launch no Git process. The native Git object format supports SHA-1 and SHA-256.

Files must be regular, present, within the project and free of link/path aliases. Size,
modification time and permissions are observed before inspection, checked again before each
batch and compared after all batches. Wrong output identities, missing lines and extra output
fail explicitly. The helper requests no filter, conversion, network fetch, object write or
index update. It rejects inherited Git environment/configuration overrides.

These are point-in-time checks, not a filesystem transaction or exclusive lock. Pause other
writers during reviewed integration; a writer that restores metadata while racing the reads
is outside that guarantee. A same-size edit made before inspection with its old timestamp
restored is detected because the bytes are hashed again.

See the official [Git hash-object interface](https://git-scm.com/docs/git-hash-object).

## Small committed blobs (0.28) {#small-committed-blobs-028}

Reviewed integration reads small committed blobs to recognize Git LFS pointers. It now fetches
raw blobs below 1024 bytes with `git cat-file --batch`, deduplicating object IDs and limiting
each request to 256 objects. A batch contains less than 256 KiB of blob content, plus headers.
The complete inventory remains bounded to 10,000 entries. Larger blobs are not loaded here.

Headers must match the exact requested object ID, type and size. Missing objects, truncated
data, inconsistent duplicate sizes and trailing output fail explicitly. Binary NUL/newline
bytes are preserved. No filter, text conversion, object write or network fetch is requested.
Existing path, LFS, attribute, checkout and receipt checks remain in place.

See the [Git batch interface](https://git-scm.com/docs/git-cat-file#_batch_output) and
[reviewed integration](doc:git-integration).
