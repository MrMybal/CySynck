#include <cysynck/core.hpp>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#ifdef __linux__
#include <sys/prctl.h>
#endif
extern char **environ;
#endif

namespace cysynck {
struct ChildProcess::Impl {
    uint64_t pid = 0;
    bool exited = false;
#ifdef _WIN32
    HANDLE process = nullptr, job = nullptr;
    ~Impl() {
        if (job)
            CloseHandle(job);
        if (process)
            CloseHandle(process);
    }
#else
    ~Impl() = default;
#endif
};
#ifdef _WIN32
static std::wstring wide_string(const std::string &value) {
    auto p = fs::u8path(value);
    return p.native();
}
static std::wstring quote_argument(const std::string &value) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (auto c : wide_string(value)) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
struct EnvironmentOrder {
    bool operator()(const std::wstring &a, const std::wstring &b) const {
        return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                    static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};
#endif
ChildProcess::ChildProcess(const std::vector<std::string> &arguments, const fs::path &cwd,
                           const std::map<std::string, std::string> &overrides)
    : impl_(std::make_unique<Impl>()) {
    if (arguments.empty() || !fs::u8path(arguments[0]).is_absolute())
        throw std::runtime_error("Child executable must have an absolute path");
#ifdef _WIN32
    std::map<std::wstring, std::wstring, EnvironmentOrder> variables;
    auto block = GetEnvironmentStringsW();
    if (!block)
        throw std::runtime_error("Cannot read child environment");
    for (auto row = block; *row; row += wcslen(row) + 1) {
        std::wstring value = row;
        auto pos = value.find(L'=');
        if (pos != std::wstring::npos && pos > 0)
            variables[value.substr(0, pos)] = value.substr(pos + 1);
    }
    FreeEnvironmentStringsW(block);
    for (auto &[key, value] : overrides)
        variables[wide_string(key)] = wide_string(value);
    std::vector<wchar_t> environment;
    for (auto &[key, value] : variables) {
        auto row = key + L'=' + value;
        environment.insert(environment.end(), row.begin(), row.end());
        environment.push_back(0);
    }
    environment.push_back(0);
    std::wstring command;
    for (auto &arg : arguments) {
        if (!command.empty())
            command += L' ';
        command += quote_argument(arg);
    }
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, 0, nullptr);
    HANDLE output = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &attributes, OPEN_EXISTING, 0, nullptr);
    if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        if (output != INVALID_HANDLE_VALUE)
            CloseHandle(output);
        throw std::runtime_error("Cannot create child streams");
    }
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!impl_->job || !SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation,
                                                &limits, sizeof(limits))) {
        CloseHandle(input);
        CloseHandle(output);
        throw std::runtime_error("Cannot create owned process job");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION info{};
    bool created =
        CreateProcessW(fs::u8path(arguments[0]).c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                       environment.data(), cwd.c_str(), &startup, &info);
    CloseHandle(input);
    CloseHandle(output);
    if (!created)
        throw std::runtime_error("Cannot start the selected executable");
    impl_->process = info.hProcess;
    impl_->pid = info.dwProcessId;
    if (!AssignProcessToJobObject(impl_->job, info.hProcess)) {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        throw std::runtime_error("Cannot isolate the child process");
    }
    if (ResumeThread(info.hThread) == static_cast<DWORD>(-1)) {
        TerminateJobObject(impl_->job, 1);
        CloseHandle(info.hThread);
        throw std::runtime_error("Cannot resume the child process");
    }
    CloseHandle(info.hThread);
#else
    std::map<std::string, std::string> variables;
    for (auto current = environ; current && *current; ++current) {
        std::string row = *current;
        auto pos = row.find('=');
        if (pos != std::string::npos)
            variables[row.substr(0, pos)] = row.substr(pos + 1);
    }
    for (auto &[key, value] : overrides)
        variables[key] = value;
    std::vector<std::string> entries;
    for (auto &[key, value] : variables)
        entries.push_back(key + '=' + value);
    std::vector<char *> argv, envp;
    for (auto &arg : arguments)
        argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    for (auto &row : entries)
        envp.push_back(row.data());
    envp.push_back(nullptr);
    int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0)
        throw std::runtime_error("Cannot create child streams");
    auto parent = getpid();
    auto pid = fork();
    if (pid < 0) {
        close(nullfd);
        throw std::runtime_error("Cannot fork owned child");
    }
    if (!pid) {
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent)
            _exit(126);
#endif
        setpgid(0, 0);
        dup2(nullfd, 0);
        dup2(nullfd, 1);
        dup2(nullfd, 2);
        close(nullfd);
        if (chdir(cwd.c_str()))
            _exit(126);
        execve(argv[0], argv.data(), envp.data());
        _exit(127);
    }
    close(nullfd);
    setpgid(pid, pid);
    impl_->pid = static_cast<uint64_t>(pid);
#endif
}
ChildProcess::~ChildProcess() {
    try {
        terminate();
    } catch (...) {
    }
}
uint64_t ChildProcess::pid() const { return impl_->pid; }
bool ChildProcess::running() {
    if (impl_->exited)
        return false;
#ifdef _WIN32
    auto result = WaitForSingleObject(impl_->process, 0);
    if (result == WAIT_FAILED)
        throw std::runtime_error("Cannot query owned child");
    impl_->exited = result == WAIT_OBJECT_0;
#else
    int status = 0;
    auto result = waitpid(static_cast<pid_t>(impl_->pid), &status, WNOHANG);
    if (result < 0 && errno != EINTR && errno != ECHILD)
        throw std::runtime_error("Cannot query owned child");
    impl_->exited = result > 0 || (result < 0 && errno == ECHILD);
#endif
    return !impl_->exited;
}
bool ChildProcess::wait(int milliseconds) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (running()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}
void ChildProcess::terminate() {
    if (!impl_ || !running())
        return;
#ifdef _WIN32
    TerminateJobObject(impl_->job, 1);
#else
    kill(-static_cast<pid_t>(impl_->pid), SIGKILL);
#endif
    wait(3000);
}
} // namespace cysynck
