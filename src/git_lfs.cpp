#include "git_lfs.hpp"
#include "backup.hpp"
#include <algorithm>
#include <charconv>
#include <map>
#include <sstream>
#include <stdexcept>

namespace cysynck {
namespace {
constexpr uintmax_t file_max = 4ull * 1024 * 1024 * 1024;
constexpr uintmax_t total_max = 16ull * 1024 * 1024 * 1024;
constexpr size_t object_max = 4096, history_max = 100000;
bool hex(const std::string &value, size_t size) {
    return value.size() == size && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}
uintmax_t number(const std::string &value) {
    uintmax_t result = 0;
    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        value != std::to_string(result))
        throw std::runtime_error("Invalid Git LFS size");
    return result;
}
Json inventory(const std::map<std::string, uintmax_t> &objects) {
    Json list = Json::array();
    uintmax_t total = 0;
    for (const auto &[digest, size] : objects) {
        if (size > file_max || total > total_max - size || objects.size() > object_max)
            throw std::runtime_error(
                "Git LFS exchange is limited to 4096 objects, 4 GiB each and 16 GiB total");
        total += size;
        list.push_back({{"sha256", digest}, {"size", size}});
    }
    return {{"version", 1}, {"scope", "branch-history"}, {"objects", list}, {"bytes", total}};
}
void pointer(const std::string &bytes, std::map<std::string, uintmax_t> &objects) {
    const std::string prefix = "version https://git-lfs.github.com/spec/v1\n";
    if (!bytes.starts_with(prefix)) {
        if (bytes.starts_with(prefix.substr(0, prefix.size() - 1)))
            throw std::runtime_error("Noncanonical Git LFS pointer requires LF line endings");
        if (bytes.starts_with("version https://hawser.github.com/spec/v1"))
            throw std::runtime_error("Legacy Git LFS pointers require conversion with Git LFS");
        return;
    }
    auto rest = bytes.substr(prefix.size());
    auto split = rest.find('\n');
    if (split == std::string::npos || !rest.starts_with("oid sha256:") ||
        !rest.substr(split + 1).starts_with("size ") || bytes.back() != '\n')
        throw std::runtime_error("Malformed Git LFS pointer or unsupported pointer extension");
    auto digest = rest.substr(11, split - 11);
    auto sizeText = rest.substr(split + 6, rest.size() - split - 7);
    if (!hex(digest, 64))
        throw std::runtime_error("Invalid Git LFS pointer digest");
    auto size = number(sizeText);
    auto [it, inserted] = objects.emplace(digest, size);
    if ((!inserted && it->second != size) || size > file_max || objects.size() > object_max)
        throw std::runtime_error("Inconsistent or oversized Git LFS pointer inventory");
}
std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
        text.pop_back();
    return text;
}
} // namespace
std::string offline_git(const Json &project, const std::vector<std::string> &args,
                        const std::string &input) {
    std::vector<std::string> command{"git",
                                     "--no-pager",
                                     "--no-replace-objects",
                                     "-c",
                                     "protocol.allow=never",
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
    auto result = process(command, {}, input, 120);
    if (result.code)
        throw std::runtime_error(
            "Offline Git object inspection failed; check repository completeness");
    return result.out;
}
Json lfs_inventory(const Json &project, const std::string &head) {
    if (!hex(head, 40) && !hex(head, 64))
        throw std::runtime_error("Invalid Git commit identity");
    if (trim(offline_git(project, {"rev-parse", "--is-shallow-repository"})) != "false")
        throw std::runtime_error(
            "Git LFS history exchange requires a complete, non-shallow repository");
    auto raw = offline_git(project, {"rev-list", "--objects", "--no-object-names", head, "--"});
    std::istringstream lines(raw);
    std::vector<std::string> names;
    std::string name;
    while (std::getline(lines, name)) {
        if ((!hex(name, 40) && !hex(name, 64)) || names.size() >= history_max)
            throw std::runtime_error(
                "Git LFS inspection is limited to 100000 reachable Git objects");
        names.push_back(name);
    }
    if (raw.empty() || raw.back() != '\n')
        throw std::runtime_error("Incomplete Git object inventory");
    std::map<std::string, uintmax_t> objects;
    for (size_t offset = 0; offset < names.size(); offset += 500) {
        auto end = std::min(names.size(), offset + 500);
        std::string input;
        for (size_t i = offset; i < end; ++i)
            input += names[i] + "\n";
        std::istringstream info(offline_git(project, {"cat-file", "--batch-check"}, input));
        std::vector<std::pair<std::string, uintmax_t>> candidates;
        for (size_t i = offset; i < end; ++i) {
            std::string id, type, size, extra, line;
            if (!std::getline(info, line))
                throw std::runtime_error("Incomplete Git object headers");
            std::istringstream header(line);
            if (!(header >> id >> type >> size) || id != names[i] || (header >> extra))
                throw std::runtime_error("Invalid Git object header");
            auto length = number(size);
            if (type == "blob" && length < 1024)
                candidates.emplace_back(id, length);
        }
        std::string extra;
        if (std::getline(info, extra))
            throw std::runtime_error("Unexpected Git object headers");
        input.clear();
        for (const auto &[id, size] : candidates)
            input += id + "\n";
        if (input.empty())
            continue;
        auto contents = offline_git(project, {"cat-file", "--batch"}, input);
        size_t position = 0;
        for (const auto &[id, size] : candidates) {
            auto header = id + " blob " + std::to_string(size) + "\n";
            if (contents.compare(position, header.size(), header) != 0)
                throw std::runtime_error("Git pointer header mismatch");
            position += header.size();
            if (position > contents.size() || size >= contents.size() - position ||
                contents[position + size] != '\n')
                throw std::runtime_error("Incomplete Git pointer contents");
            pointer(contents.substr(position, static_cast<size_t>(size)), objects);
            position += static_cast<size_t>(size) + 1;
        }
        if (position != contents.size())
            throw std::runtime_error("Unexpected Git pointer contents");
    }
    return inventory(objects);
}
Json validate_lfs_manifest(const Json &manifest) {
    if (!manifest.is_object() || manifest.at("version") != 1 ||
        manifest.at("scope") != "branch-history" || !manifest.at("objects").is_array() ||
        manifest.at("objects").size() > object_max)
        throw std::runtime_error("Unsupported Git LFS inventory");
    std::map<std::string, uintmax_t> objects;
    for (const auto &object : manifest.at("objects")) {
        auto digest = object.at("sha256").get<std::string>();
        const auto &size = object.at("size");
        if (!hex(digest, 64) || !size.is_number_integer() ||
            (!size.is_number_unsigned() && size.get<int64_t>() < 0) ||
            !objects.emplace(digest, size.get<uintmax_t>()).second)
            throw std::runtime_error("Invalid or duplicated Git LFS object");
    }
    auto result = inventory(objects);
    if (result != manifest)
        throw std::runtime_error("Noncanonical or inconsistent Git LFS inventory");
    return result;
}
fs::path lfs_local_store(const Json &project) {
    // Custom/shared storage needs a separately reviewed path policy; never infer it from peer data.
    auto configuration = offline_git(project, {"config", "--default=", "--get", "lfs.storage"});
    if (!trim(configuration).empty())
        throw std::runtime_error("Custom lfs.storage is not supported for offline exchange");
    auto root = fs::u8path(project.at("root").get<std::string>());
    auto common = fs::u8path(trim(offline_git(project, {"rev-parse", "--git-common-dir"})));
    if (!common.is_absolute())
        common = root / common;
    common = fs::absolute(common).lexically_normal();
    if (path_is_link(common) || !fs::is_directory(common))
        throw std::runtime_error("Git common directory is missing or linked");
    auto store = project_child(common, "lfs/objects");
    plain_directory(store);
    return store;
}
fs::path lfs_object_path(const fs::path &store, const std::string &digest) {
    if (!hex(digest, 64))
        throw std::runtime_error("Invalid Git LFS object identity");
    return project_child(store, digest.substr(0, 2) + "/" + digest.substr(2, 2) + "/" + digest);
}
void verify_lfs_object(const fs::path &path, const Json &object) {
    if (path_is_link(path) || !fs::is_regular_file(path) ||
        fs::file_size(path) != object.at("size").get<uintmax_t>() ||
        sha256_file(path) != object.at("sha256").get<std::string>())
        throw std::runtime_error("Git LFS object is missing, linked or corrupted");
}
void copy_lfs_object(const fs::path &source, const fs::path &store, const Json &object) {
    auto destination = lfs_object_path(store, object.at("sha256"));
    verify_lfs_object(source, object);
    if (fs::exists(destination)) {
        verify_lfs_object(destination, object);
        return;
    }
    fs::create_directories(destination.parent_path());
    secure_state_path(destination.parent_path(), true);
    auto temp = destination.parent_path() / ("cysynck-" + random_id() + ".part");
    try {
        fs::copy_file(source, temp, fs::copy_options::none);
        secure_state_path(temp, false);
        verify_lfs_object(temp, object);
        // A hard link publishes atomically without replacing a concurrent writer's object.
        std::error_code error;
        fs::create_hard_link(temp, destination, error);
        if (error) {
            if (!fs::exists(destination))
                throw fs::filesystem_error("Cannot publish Git LFS object", destination, error);
            verify_lfs_object(destination, object);
        }
        fs::remove(temp);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}
Json lfs_availability(const fs::path &store, const Json &inventory) {
    size_t missing = 0;
    for (const auto &object : inventory.at("objects")) {
        auto path = lfs_object_path(store, object.at("sha256"));
        if (!fs::is_regular_file(path) || path_is_link(path) ||
            fs::file_size(path) != object.at("size").get<uintmax_t>())
            ++missing;
    }
    return {{"objects", inventory.at("objects").size()},
            {"bytes", inventory.at("bytes")},
            {"missing", missing},
            {"complete", missing == 0}};
}
} // namespace cysynck
