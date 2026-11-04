# Internal subprocess streaming

CySynck 0.18 adds `process_stream` in the internal `src/process_stream.hpp` header. The installed
SDK continues to use its existing C/C++ request APIs; this helper is not an installed SDK API.

The helper accepts argument vectors, a stdout sink, working directory, buffered stdin and a
timeout. Arguments are passed directly to the executable. Its reader invokes the sink with
at most 16 KiB of bytes at a time; stdout stays absent from `ProcessResult.out`. Stderr is
drained on a separate thread, retaining at most 8 MiB. The original `process` capture API keeps
its 8 MiB stdout/stderr limits and still drains excess output.

The sink runs on the stdout reader thread. Its state may be inspected after the helper returns,
when all reader/writer threads have joined. A sink exception cancels the owned process and
is rethrown on the calling thread. Timeouts also cancel the process. Partial sink output is
the caller's responsibility; Git extraction records it as incomplete. Nonzero executable exit
codes remain in the result and must be checked by the caller.

Windows streaming requires a process job and refuses launch when isolation cannot be established.
Job/process handles remain valid through reader shutdown. Owned descendants are terminated
before joining pipe readers, including when the leader exits normally. POSIX streaming uses
a separate process group; `waitid` with `WNOWAIT` keeps the leader waitable until readers finish
so its PID cannot be recycled while failure handlers can still cancel the group. Existing
POSIX capture calls retain their normal descendant behavior on successful exit.

Callbacks must return promptly. The timeout controls the child, not arbitrary blocking work
inside a callback. This helper also does not make buffered stdin a streaming source. Git
extraction supplies no stdin payload and writes bounded chunks to an exclusively created file.

The dedicated fixture executable tests binary output beyond the capture bound, simultaneous
input/output, overflowing diagnostics, nonzero status, sink exceptions, timeout, Unicode working
directories and inherited descendant pipes. The Git suite exercises an actual 101 MiB binary
file and a metadata-only 1 GiB boundary; the latter does not claim a 1 GiB runtime benchmark.
Native POSIX execution remains part of the pending Linux/macOS platform validation.
