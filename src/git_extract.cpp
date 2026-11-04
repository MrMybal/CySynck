#include "git_extract.hpp"
#include "backup.hpp"
#include "git_lfs.hpp"
#include "git_review.hpp"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <regex>
#include <sstream>
#include <picosha2.h>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace cysynck {
namespace {
constexpr uintmax_t blob_limit = 1024ull * 1024 * 1024;
constexpr uintmax_t total_limit = 16ull * 1024 * 1024 * 1024;
struct PathLess {
    bool operator()(const std::string &a, const std::string &b) const {
#ifdef _WIN32
        auto x = fs::u8path(a).native(), y = fs::u8path(b).native();
        return CompareStringOrdinal(x.c_str(), static_cast<int>(x.size()), y.c_str(),
                                    static_cast<int>(y.size()), TRUE) == CSTR_LESS_THAN;
#else
        auto fold = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [&](unsigned char x, unsigned char y) { return fold(x) < fold(y); });
#endif
    }
};
uintmax_t number(const std::string &text) {
    uintmax_t n = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Invalid Git extraction size");
    return n;
}
std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}
std::string blob(const Json &project, const Json &entry) {
    auto bytes = offline_git(project, {"cat-file", "blob", entry.at("oid")});
    if (bytes.size() != entry.at("blob_bytes").get<uintmax_t>() ||
        entry.at("oid") != trim(offline_git(project, {"hash-object", "--stdin"}, bytes)))
        throw std::runtime_error("Git extraction blob is missing, corrupted or truncated");
    return bytes;
}
void validate_path(const std::string &name, std::map<std::string, std::string, PathLess> &paths) {
    auto path = fs::u8path(name);
    (void)Json(name).dump();
    if (name.empty() || name.size() > 4096 || name.front() == '/' || name.back() == '/' ||
        name.find("//") != name.npos || name.find_first_of("\\:") != name.npos ||
        name.find('\0') != name.npos || path.is_absolute())
        throw std::runtime_error("Git extraction contains an unsafe relative path");
    fs::path prefix;
    for (const auto &part : path) {
        auto segment = path_string(part), lower = segment;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
        });
        if (segment.empty() || segment == "." || segment == ".." || lower == ".git" ||
            std::regex_match(lower, std::regex("git~[0-9]+")))
            throw std::runtime_error("Git extraction excludes repository metadata paths");
#ifdef _WIN32
        if (segment.back() == '.' || segment.back() == ' ' ||
            segment.find_first_of("<>\"|?*") != segment.npos ||
            std::any_of(segment.begin(), segment.end(), [](unsigned char c) { return c < 32; }) ||
            std::regex_match(segment, std::regex("(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?",
                                                 std::regex::icase)))
            throw std::runtime_error("Git extraction contains a reserved Windows filename");
#endif
        prefix /= part;
        auto spelling = path_string(prefix);
        auto [it, inserted] = paths.emplace(spelling, spelling);
        if (!inserted && it->second != spelling)
            throw std::runtime_error("Git extraction paths collide on this platform");
    }
}
Json inspect(const fs::path &state, const Json &projects, const Json &project, const Json &data) {
    auto root = fs::absolute(fs::u8path(project.at("root").get<std::string>()));
    fs::path current;
    for (const auto &part : root) {
        current /= part;
        plain_directory(current);
    }
    if (!fs::is_directory(root))
        throw std::runtime_error("Git extraction project directory is missing");
    auto revision = git_revision_context(project, data);
    auto target = restore_target(state, projects, data);
    bool hydrate = data.value("lfs", true);
    auto raw = offline_git(
        project, {"ls-tree", "-r", "-z", "--full-tree", "--long", revision.at("commit"), "--"});
    if (raw.size() >= 8 * 1024 * 1024)
        throw std::runtime_error("Git extraction tree listing exceeds the process output bound");
    Json entries = Json::array(), errors = Json::array();
    std::map<std::string, std::string, PathLess> paths;
    std::set<std::string, PathLess> files;
    std::map<std::string, uintmax_t> assets;
    fs::path store;
    uintmax_t total = 0, lfsBytes = 0, lfsFiles = 0;
    size_t position = 0;
    while (position < raw.size()) {
        auto end = raw.find('\0', position), tab = raw.find('\t', position);
        if (end == raw.npos || tab == raw.npos || tab >= end || entries.size() >= 10000)
            throw std::runtime_error("Invalid Git extraction listing or more than 10000 files");
        std::istringstream header(raw.substr(position, tab - position));
        std::string mode, type, oid, size, extra;
        if (!(header >> mode >> type >> oid >> size) || (header >> extra) ||
            (oid.size() != 40 && oid.size() != 64) ||
            oid.find_first_not_of("0123456789abcdef") != oid.npos)
            throw std::runtime_error("Invalid Git extraction object metadata");
        auto name = raw.substr(tab + 1, end - tab - 1);
        validate_path(name, paths);
        if (!files.insert(name).second)
            throw std::runtime_error("Git extraction contains duplicate file paths");
        position = end + 1;
        Json entry = {{"path", name},    {"mode", mode}, {"oid", oid},
                      {"blob_bytes", 0}, {"bytes", 0},   {"lfs", nullptr}};
        try {
            if ((mode != "100644" && mode != "100755") || type != "blob")
                throw std::runtime_error("Git extraction supports regular files; links and "
                                         "submodules require manual handling");
            auto bytes = number(size);
            entry["blob_bytes"] = entry["bytes"] = bytes;
            if (bytes > blob_limit)
                throw std::runtime_error("Ordinary Git blobs are limited to 1 GiB per file");
            if (hydrate && bytes < 1024) {
                auto pointer = git_lfs_pointer(blob(project, entry));
                if (!pointer.is_null()) {
                    auto length = pointer.at("size").get<uintmax_t>();
                    if (length > 4ull * 1024 * 1024 * 1024)
                        throw std::runtime_error(
                            "Git extraction LFS assets are limited to 4 GiB each");
                    if (store.empty())
                        store = lfs_local_store(project);
                    auto digest = pointer.at("sha256").get<std::string>();
                    auto [it, inserted] = assets.emplace(digest, length);
                    if (assets.size() > 4096 || (!inserted && it->second != length))
                        throw std::runtime_error(
                            "Git extraction has inconsistent or too many LFS objects");
                    if (inserted)
                        verify_lfs_object(lfs_object_path(store, digest), pointer);
                    entry["lfs"] = pointer;
                    entry["bytes"] = bytes = length;
                    ++lfsFiles;
                    lfsBytes += length;
                }
            }
            if (bytes > total_limit - total)
                throw std::runtime_error("Git extraction is limited to 16 GiB of output");
            total += bytes;
        } catch (const std::exception &error) {
            errors.push_back({{"path", name}, {"error", error.what()}});
        }
        entries.push_back(entry);
    }
    for (const auto &entry : entries) {
        auto parent = fs::u8path(entry.at("path").get<std::string>()).parent_path();
        for (; !parent.empty(); parent = parent.parent_path())
            if (files.contains(path_string(parent)))
                throw std::runtime_error("Git extraction contains a file/directory collision");
    }
    // A ref change during inspection invalidates the preview even if objects remain available.
    (void)git_revision_context(project, revision);
    Json result = revision;
    result.update({{"project", project.at("id")},
                   {"destination", path_string(target)},
                   {"root", project.at("root")},
                   {"lfs", hydrate},
                   {"lfs_storage", store.empty() ? Json(nullptr) : Json(path_string(store))},
                   {"files", entries},
                   {"file_count", entries.size()},
                   {"bytes", total},
                   {"lfs_files", lfsFiles},
                   {"lfs_bytes", lfsBytes},
                   {"errors", errors},
                   {"can_extract", errors.empty()},
                   {"applied", false}});
    result["plan"] = errors.empty() ? Json(sha256(result.dump())) : Json(nullptr);
    return result;
}
} // namespace
Json extract_git_revision(const fs::path &state, const Json &projects, const Json &project,
                          const Json &data) {
    auto result = inspect(state, projects, project, data);
    if (!data.value("apply", false))
        return result;
    if (!result.at("can_extract").get<bool>() || !data.contains("plan") ||
        data.at("plan") != result.at("plan"))
        throw std::runtime_error("Git extraction is blocked or its plan changed; preview again");
    auto target = fs::u8path(result.at("destination").get<std::string>());
    auto receipts = project_child(state, "git-extractions/" + project.at("id").get<std::string>());
    plain_directory(receipts);
    fs::create_directories(receipts);
    secure_state_path(receipts, true);
    auto id = random_id();
    auto receipt = receipts / (id + ".json");
    Json record = result;
    record.update(
        {{"id", id}, {"created", timestamp()}, {"status", "incomplete"}, {"files_written", 0}});
    atomic_write(receipt, record.dump(2), true);
    try {
        fs::create_directories(target.parent_path());
        if (restore_target(state, projects, data) != target || !fs::create_directory(target))
            throw std::runtime_error("Git extraction destination appeared or changed");
        for (auto &entry : record["files"]) {
            auto destination = project_child(target, entry.at("path"));
            fs::create_directories(destination.parent_path());
            destination = project_child(target, entry.at("path"));
            FILE *file = nullptr;
#ifdef _WIN32
            _wfopen_s(&file, destination.c_str(), L"wbx");
#else
            file = std::fopen(destination.c_str(), "wbx");
#endif
            if (!file)
                throw std::runtime_error("Cannot create extracted file exclusively");
            std::unique_ptr<FILE, decltype(&std::fclose)> output(file, std::fclose);
            secure_state_path(destination, false);
            auto write = [&](const char *bytes, size_t count) {
                if (std::fwrite(bytes, 1, count, output.get()) != count)
                    throw std::runtime_error("Extracted file write failed");
            };
            std::string expected;
            if (entry.at("lfs").is_null()) {
                picosha2::hash256_one_by_one hash;
                uintmax_t copied = 0;
                const auto length = entry.at("blob_bytes").get<uintmax_t>();
                offline_git_stream(project, {"cat-file", "blob", entry.at("oid")},
                                   [&](const char *bytes, size_t count) {
                                       if (count > length - copied)
                                           throw std::runtime_error(
                                               "Git blob exceeded its reviewed size");
                                       write(bytes, count);
                                       hash.process(bytes, bytes + count);
                                       copied += count;
                                   });
                if (copied != length)
                    throw std::runtime_error("Git blob ended before its reviewed size");
                hash.finish();
                expected = picosha2::get_hash_hex_string(hash);
            } else {
                auto store = lfs_local_store(project);
                if (Json(path_string(store)) != result.at("lfs_storage"))
                    throw std::runtime_error("Git LFS storage changed during extraction");
                const auto &asset = entry.at("lfs");
                auto source = lfs_object_path(store, asset.at("sha256"));
                verify_lfs_object(source, asset);
                std::ifstream input(source, std::ios::binary);
                std::vector<char> buffer(1024 * 1024);
                uintmax_t copied = 0;
                while (input) {
                    input.read(buffer.data(), buffer.size());
                    auto count = static_cast<size_t>(input.gcount());
                    if (count > asset.at("size").get<uintmax_t>() - copied)
                        throw std::runtime_error("Git LFS asset grew during extraction");
                    write(buffer.data(), count);
                    copied += count;
                }
                if (!input.eof() || copied != asset.at("size").get<uintmax_t>())
                    throw std::runtime_error("Git LFS asset changed during extraction");
                expected = asset.at("sha256");
            }
            if (std::fflush(output.get()))
                throw std::runtime_error("Cannot flush extracted file");
            output.reset();
            if (entry.at("lfs").is_null() &&
                entry.at("oid") != trim(offline_git(project, {"hash-object", "--no-filters", "--",
                                                              path_string(destination)})))
                throw std::runtime_error("Extracted Git blob identity mismatch");
            if (fs::file_size(destination) != entry.at("bytes").get<uintmax_t>() ||
                sha256_file(destination) != expected)
                throw std::runtime_error("Extracted file checksum mismatch");
            fs::permissions(destination,
                            static_cast<fs::perms>(entry.at("mode") == "100755" ? 0755 : 0644));
            entry["sha256"] = expected;
            record["files_written"] = record.at("files_written").get<size_t>() + 1;
        }
        (void)git_revision_context(project, result);
        record["status"] = "complete";
        record["applied"] = true;
        record["plan"] = nullptr;
        atomic_write(receipt, record.dump(2), true);
    } catch (const std::exception &error) {
        record["status"] = "incomplete";
        record["applied"] = false;
        record["error"] = error.what();
        atomic_write(receipt, record.dump(2), true);
        throw std::runtime_error("Git extraction incomplete; partial files were preserved at " +
                                 path_string(target) + "; receipt " + id + ": " + error.what());
    }
    result = record;
    result["receipt"] = id;
    result["receipt_path"] = path_string(receipt);
    return result;
}
} // namespace cysynck
