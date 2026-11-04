#include <array>
#include <chrono>
#include <cysynck/core.hpp>
#include <fstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cysynck {
std::string path_string(const fs::path &path) {
    auto s = path.generic_u8string();
    return {s.begin(), s.end()};
}
#ifdef _WIN32
static std::wstring wide(const std::string &value) {
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                   static_cast<int>(value.size()), nullptr, 0);
    if (!size && !value.empty())
        throw std::runtime_error("Invalid UTF-8 process argument");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size);
    return result;
}
static std::wstring quote(const std::string &value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : wide(value)) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    result += L'\"';
    return result;
}
#endif
ProcessResult process(const std::vector<std::string> &args, const fs::path &cwd,
                      const std::string &input, int timeout) {
    if (args.empty())
        throw std::runtime_error("No process command");
    ProcessResult result{};
    constexpr size_t cap = 8 * 1024 * 1024;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outRead, outWrite, errRead, errWrite, inRead, inWrite;
    if (!CreatePipe(&outRead, &outWrite, &security, 0))
        throw std::runtime_error("Cannot create process output pipe");
    if (!CreatePipe(&errRead, &errWrite, &security, 0)) {
        CloseHandle(outRead);
        CloseHandle(outWrite);
        throw std::runtime_error("Cannot create process error pipe");
    }
    if (!CreatePipe(&inRead, &inWrite, &security, 0)) {
        CloseHandle(outRead);
        CloseHandle(outWrite);
        CloseHandle(errRead);
        CloseHandle(errWrite);
        throw std::runtime_error("Cannot create process input pipe");
    }
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inRead;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    PROCESS_INFORMATION info{};
    std::wstring command;
    for (const auto &arg : args) {
        if (!command.empty())
            command += L' ';
        command += quote(arg);
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job)
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    bool created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                  cwd.empty() ? nullptr : cwd.c_str(), &startup, &info);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    CloseHandle(inRead);
    if (!created) {
        CloseHandle(outRead);
        CloseHandle(errRead);
        CloseHandle(inWrite);
        if (job)
            CloseHandle(job);
        throw std::runtime_error("Could not launch " + args.front() +
                                 " (install it or specify its full path)");
    }
    if (job && !AssignProcessToJobObject(job, info.hProcess)) {
        CloseHandle(job);
        job = nullptr;
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    auto read = [&](HANDLE handle, std::string &output) {
        std::array<char, 16384> buffer{};
        DWORD count;
        while (
            ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) &&
            count) {
            if (output.size() < cap)
                output.append(buffer.data(), std::min<size_t>(count, cap - output.size()));
        }
        CloseHandle(handle);
    };
    std::thread output(read, outRead, std::ref(result.out)),
        error(read, errRead, std::ref(result.error));
    std::thread writer([&] {
        DWORD written;
        size_t offset = 0;
        while (offset < input.size() &&
               WriteFile(inWrite, input.data() + offset,
                         static_cast<DWORD>(std::min<size_t>(input.size() - offset, 16384)),
                         &written, nullptr) &&
               written)
            offset += written;
        CloseHandle(inWrite);
    });
    bool timedOut = WaitForSingleObject(info.hProcess, timeout * 1000) == WAIT_TIMEOUT;
    if (timedOut) {
        if (job)
            TerminateJobObject(job, 1);
        else
            TerminateProcess(info.hProcess, 1);
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code;
    GetExitCodeProcess(info.hProcess, &code);
    if (job)
        CloseHandle(job);
    CloseHandle(info.hProcess);
    writer.join();
    output.join();
    error.join();
    result.code = static_cast<int>(code);
    if (timedOut)
        throw std::runtime_error("Process timed out: " + args.front());
#else
    int outPipe[2], errPipe[2], inPipe[2];
    if (pipe(outPipe))
        throw std::runtime_error("Cannot create process output pipe");
    if (pipe(errPipe)) {
        close(outPipe[0]);
        close(outPipe[1]);
        throw std::runtime_error("Cannot create process error pipe");
    }
    if (pipe(inPipe)) {
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]})
            close(fd);
        throw std::runtime_error("Cannot create process input pipe");
    }
    std::vector<char *> argv;
    for (const auto &arg : args)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    auto cwdString = cwd.string();
    pid_t pid = fork();
    if (pid < 0) {
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1], inPipe[0], inPipe[1]})
            close(fd);
        throw std::runtime_error("Cannot fork process");
    }
    if (!pid) {
        setpgid(0, 0);
        dup2(outPipe[1], 1);
        dup2(errPipe[1], 2);
        dup2(inPipe[0], 0);
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1], inPipe[0], inPipe[1]})
            close(fd);
        if (!cwdString.empty() && chdir(cwdString.c_str()))
            _exit(126);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(outPipe[1]);
    close(errPipe[1]);
    close(inPipe[0]);
    auto read = [&](int fd, std::string &text) {
        std::array<char, 16384> buffer;
        ssize_t count;
        while ((count = ::read(fd, buffer.data(), buffer.size())) > 0)
            if (text.size() < cap)
                text.append(buffer.data(), std::min<size_t>(count, cap - text.size()));
        close(fd);
    };
    std::thread output(read, outPipe[0], std::ref(result.out)),
        error(read, errPipe[0], std::ref(result.error));
    std::thread writer([&] {
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &signals, nullptr);
        size_t offset = 0;
        while (offset < input.size()) {
            auto n = write(inPipe[1], input.data() + offset, input.size() - offset);
            if (n <= 0)
                break;
            offset += n;
        }
        close(inPipe[1]);
    });
    int code = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    bool timedOut = false;
    while (waitpid(pid, &code, WNOHANG) == 0) {
        if (std::chrono::steady_clock::now() > deadline) {
            kill(-pid, SIGKILL);
            waitpid(pid, &code, 0);
            timedOut = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    writer.join();
    output.join();
    error.join();
    result.code = WIFEXITED(code) ? WEXITSTATUS(code) : 128;
    if (timedOut)
        throw std::runtime_error("Process timed out: " + args.front());
#endif
    return result;
}

DiskLock::DiskLock(const fs::path &state, const std::string &filename, bool wait) {
#ifdef _WIN32
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (true) {
        auto h = CreateFileW((state / filename).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            handle_ = h;
            break;
        }
        if (!wait || GetLastError() != ERROR_SHARING_VIOLATION ||
            std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Cannot lock application state");
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
#else
    fd_ = open((state / filename).c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd_ < 0 || flock(fd_, LOCK_EX | (wait ? 0 : LOCK_NB))) {
        if (fd_ >= 0)
            close(fd_);
        throw std::runtime_error("Cannot lock application state");
    }
#endif
}
DiskLock::~DiskLock() {
#ifdef _WIN32
    if (handle_)
        CloseHandle(handle_);
#else
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
    }
#endif
}
std::string git(const Json &project, const std::vector<std::string> &args) {
    std::vector<std::string> command{"git",
                                     "--no-pager",
                                     "-c",
                                     "core.hooksPath="
#ifdef _WIN32
                                     "NUL"
#else
                                     "/dev/null"
#endif
                                     ,
                                     "-C",
                                     project.at("root").get<std::string>()};
    command.insert(command.end(), args.begin(), args.end());
    auto result = process(command, {}, {}, 120);
    if (result.code)
        throw std::runtime_error(result.error.empty() ? "Git operation failed"
                                                      : result.error.substr(0, 2000));
    while (!result.out.empty() && (result.out.back() == '\n' || result.out.back() == '\r'))
        result.out.pop_back();
    return result.out;
}
} // namespace cysynck
