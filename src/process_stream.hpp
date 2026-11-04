#pragma once
#include <cysynck/core.hpp>
#include <functional>
namespace cysynck {
using ProcessSink = std::function<void(const char *, size_t)>;
// The sink runs on a reader thread. Exceptions cancel the owned child and are rethrown
// after the streams have closed. Streamed stdout is never accumulated in ProcessResult.
ProcessResult process_stream(const std::vector<std::string> &arguments, const ProcessSink &sink,
                             const fs::path &cwd = {}, const std::string &input = {},
                             int timeout_seconds = 60);
} // namespace cysynck
