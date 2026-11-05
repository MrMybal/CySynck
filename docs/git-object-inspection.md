# Bounded Git object inspection

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
