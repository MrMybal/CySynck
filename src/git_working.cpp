#include "git_working.hpp"
#include "backup.hpp"
#include "git_lfs.hpp"
#include "git_paths.hpp"
#include <set>

namespace cysynck {
namespace {
constexpr uintmax_t file_limit = 1024ull * 1024 * 1024;
constexpr uintmax_t total_limit = 16ull * 1024 * 1024 * 1024;
constexpr uintmax_t batch_bytes = 32ull * 1024 * 1024;
constexpr size_t batch_paths = 128, argument_limit = 12000;
bool oid(const std::string &value) {
    return (value.size() == 40 || value.size() == 64) &&
           value.find_first_not_of("0123456789abcdef") == value.npos;
}
struct Observation {
    uintmax_t bytes;
    fs::file_time_type modified;
    fs::perms permissions;
    bool operator==(const Observation &) const = default;
};
Observation observe(const fs::path &root, const std::string &name) {
    auto path = project_child(root, name);
    auto status = fs::symlink_status(path);
    if (path_is_link(path) || !fs::is_regular_file(status))
        throw std::runtime_error("Tracked Git working file is missing, linked or not regular");
    return {fs::file_size(path), fs::last_write_time(path), status.permissions()};
}
struct File {
    std::string name, absolute;
    size_t oid_length;
    Observation before;
};
} // namespace
GitWorkingHashes git_working_hashes(const Json &project, const Json &files) {
    if (!files.is_array() || files.size() > 10000)
        throw std::runtime_error("Git working hashes require an array of at most 10000 files");
    require_default_git_environment();
    for (auto key : {"GIT_CONFIG_PARAMETERS", "GIT_CONFIG_COUNT", "GIT_ATTR_SOURCE", "GIT_CONFIG",
                     "GIT_CONFIG_GLOBAL", "GIT_CONFIG_SYSTEM"})
        if (!env(key).empty())
            throw std::runtime_error(
                "Git working hashes require Git configuration overrides to be unset");
    auto root = fs::absolute(fs::u8path(project.at("root").get<std::string>()));
    fs::path parent;
    for (const auto &part : root) {
        parent /= part;
        plain_directory(parent);
    }
    GitWorkingHashes result;
    std::vector<File> selected;
    std::map<std::string, std::string, GitPathLess> paths;
    std::set<std::string> names;
    for (const auto &file : files) {
        auto name = file.at("path").get<std::string>();
        auto expected = file.at("oid").get<std::string>();
        const auto &size = file.at("bytes");
        if (!oid(expected) || !size.is_number_integer() ||
            (!size.is_number_unsigned() && size.get<int64_t>() < 0) ||
            size.get<uintmax_t>() > file_limit || !names.insert(name).second)
            throw std::runtime_error("Invalid or duplicate Git working hash metadata");
        validate_git_path(name, paths);
        auto absolute = path_string(project_child(root, name));
        if (absolute.size() + 3 > argument_limit)
            throw std::runtime_error("Git working file path exceeds the batch argument bound");
        auto before = observe(root, name);
        if (before.bytes != size.get<uintmax_t>())
            throw std::runtime_error(
                "Tracked Git working file size differs from its committed blob");
        if (before.bytes > total_limit - result.bytes)
            throw std::runtime_error("Git working hash files exceed 16 GiB");
        result.bytes += before.bytes;
        selected.push_back({name, absolute, expected.size(), before});
    }
    for (size_t first = 0; first < selected.size();) {
        size_t last = first, arguments = 0;
        uintmax_t bytes = 0;
        std::vector<std::string> command{"--no-optional-locks", "hash-object", "--no-filters",
                                         "--"};
        while (last < selected.size() && last - first < batch_paths) {
            const auto &file = selected.at(last);
            if (last != first &&
                (arguments + file.absolute.size() + 3 > argument_limit ||
                 file.before.bytes > batch_bytes || bytes > batch_bytes - file.before.bytes))
                break;
            if (observe(root, file.name) != file.before)
                throw std::runtime_error("Git working file changed before its hash batch");
            command.push_back(file.absolute);
            arguments += file.absolute.size() + 3;
            bytes += file.before.bytes;
            ++last;
        }
        // Arguments are separate process parameters, including for names with newlines on POSIX.
        auto output = offline_git(project, command);
        ++result.batches;
        size_t position = 0;
        for (size_t i = first; i < last; ++i) {
            auto end = output.find('\n', position);
            if (end == output.npos)
                throw std::runtime_error("Incomplete Git working hash batch output");
            auto digest = output.substr(position, end - position);
            if (!digest.empty() && digest.back() == '\r')
                digest.pop_back();
            if (!oid(digest) || digest.size() != selected.at(i).oid_length)
                throw std::runtime_error("Invalid Git working hash batch identity");
            result.ids.emplace(selected.at(i).name, digest);
            position = end + 1;
        }
        if (position != output.size())
            throw std::runtime_error("Unexpected Git working hash batch data");
        first = last;
    }
    for (const auto &file : selected)
        if (observe(root, file.name) != file.before)
            throw std::runtime_error("Git working file changed during batch inspection");
    return result;
}
} // namespace cysynck
