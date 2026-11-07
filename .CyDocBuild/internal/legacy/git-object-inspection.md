# Bounded Git object inspection

## Raw working files (0.35)

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

The dedicated fixture checks 260 raw files per object format, including empty/binary/CRLF
content, Unicode and spaces, duplicate content, same-size edits with restored timestamps,
and a separate 33 MiB file. It compares individual Git hashes, checks that configured filters
never run, and snapshots Git metadata to verify inspection does not write repository state.
POSIX newline/tab/quote names are covered in the source fixture; they await native execution.
Symlink refusal is exercised where the host permits creating the fixture link.

One Windows run checked 260 files in three Git invocations for each object format. For a
32-file sample, individual reads took 2,261 ms versus 102 ms batched in SHA-1, and 2,788 ms
versus 154 ms in SHA-256. These are local working-hash measurements, not whole-integration
or synchronization throughput. Timing is reported without a pass/fail threshold. That host
did not permit creating test symlinks; existing link checks remain in the implementation.

See the official [Git hash-object interface](https://git-scm.com/docs/git-hash-object).

## Small committed blobs (0.28)

Reviewed integration reads small committed blobs to recognize Git LFS pointers. It now fetches
raw blobs below 1024 bytes with `git cat-file --batch`, deduplicating object IDs and limiting
each request to 256 objects. A batch contains less than 256 KiB of blob content, plus headers.
The complete inventory remains bounded to 10,000 entries. Larger blobs are not loaded here.

Headers must match the exact requested object ID, type and size. Missing objects, truncated
data, inconsistent duplicate sizes and trailing output fail explicitly. Binary NUL/newline
bytes are preserved. No filter, text conversion, object write or network fetch is requested.
Existing path, LFS, attribute, checkout and receipt checks remain in place.

The dedicated Windows fixture compares 260 distinct objects with individual raw Git reads,
including empty and 1023-byte blobs and duplicate references, in SHA-1 and SHA-256 repositories.
One local run measured 10,578 ms versus 117 ms for SHA-1, and 14,434 ms versus 165 ms for SHA-256.
These timings measure the object-read stage on a small owned fixture, not whole-project
integration throughput. Timing is reported rather than asserted because host load varies.

See the [Git batch interface](https://git-scm.com/docs/git-cat-file#_batch_output) and
[reviewed integration](git-integration.md).
