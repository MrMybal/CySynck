#include "cyrevision_exchange.hpp"
#include "cyrevision_codec.hpp"
#include "backup.hpp"
#include "git_lfs.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cysynck {
namespace {
namespace cr = cyrevision;
using cr::Document;
constexpr uintmax_t bundle_limit = 16ull * 1024 * 1024 * 1024;
constexpr size_t publication_limit = 4096;
std::string compact(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '-'), value.end());
    return value;
}
std::string new_guid() {
    auto value = random_id();
    value[12] = '4';
    value[16] = '8';
    return value.substr(0, 8) + "-" + value.substr(8, 4) + "-" + value.substr(12, 4) + "-" +
           value.substr(16, 4) + "-" + value.substr(20);
}
std::string trim(std::string value) {
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
    return value;
}
Document read_json(const fs::path &path) {
    if (path_is_link(path) || !fs::is_regular_file(path))
        throw std::runtime_error("CyRevision metadata is missing or linked");
    return cr::parse(read_file(path));
}
fs::path directory(const fs::path &parent, const std::string &relative, bool create = true) {
    auto path = project_child(parent, relative);
    plain_directory(path);
    if (create) {
        fs::create_directories(path);
        secure_state_path(path, true);
    }
    return path;
}
Document local_identity(const fs::path &home, const Json &data, bool create) {
    auto path = project_child(home, "identity.json"), key = project_child(home, "signing.pem");
    if (!fs::exists(path)) {
        if (!create)
            throw std::runtime_error("Create a CyRevision publishing identity first");
        auto name = data.at("name").get<std::string>();
        // Validate the public identity before generating or persisting any private key.
        if (name.empty() || name.size() > 160 || name.find('\0') != std::string::npos)
            throw std::runtime_error("CyRevision display name requires 1 to 160 bytes");
        if (!fs::exists(key))
            cr::generate_key(key);
        auto value = cr::identity({{"userId", new_guid()},
                                   {"deviceId", new_guid()},
                                   {"displayName", name},
                                   {"syncthingDeviceId", ""},
                                   {"signingPublicKey", cr::public_key(key)}});
        atomic_write(path, value.dump(2), true);
    }
    auto value = cr::identity(read_json(path));
    if (value.at("signingPublicKey") != cr::public_key(key))
        throw std::runtime_error("CyRevision public identity differs from its private signing key");
    return value;
}
Json public_identity(const Document &value) {
    return {{"identity", Json::parse(value.dump())},
            {"fingerprint", cr::fingerprint(value.at("signingPublicKey").get<std::string>())}};
}
Document settings(const fs::path &home) {
    auto file = project_child(home, "settings.json");
    return fs::exists(file) ? read_json(file) : Document{{"devices", Document::object()}};
}
std::string pinned_key(const Document &config, const Document &identity) {
    auto device = identity.at("deviceId").get<std::string>();
    if (!config.at("devices").contains(device))
        throw std::runtime_error("CyRevision publishing device is not trusted");
    auto pinned = cr::identity(config.at("devices").at(device));
    if (pinned.at("userId") != identity.at("userId") ||
        pinned.at("signingPublicKey") != identity.at("signingPublicKey"))
        throw std::runtime_error("CyRevision publishing identity differs from the pinned device");
    return pinned.at("signingPublicKey").get<std::string>();
}
void branches_valid(const Json &project, const Document &branches) {
    for (const auto &[name, value] : branches.items())
        if (offline_git_result(project, {"check-ref-format", "refs/heads/" + name}).code)
            throw std::runtime_error("CyRevision publication contains an invalid Git branch");
}
Document bundle_heads(const Json &project, const fs::path &path) {
    std::istringstream lines(offline_git(project, {"bundle", "list-heads", path_string(path)}));
    Document result = Document::object();
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        auto split = line.find(' ');
        if (split == std::string::npos)
            throw std::runtime_error("Invalid Git bundle reference");
        auto ref = line.substr(split + 1);
        if (!ref.starts_with("refs/heads/"))
            throw std::runtime_error("CyRevision bundle contains non-branch references");
        auto branch = ref.substr(11);
        if (result.contains(branch))
            throw std::runtime_error("Duplicate Git bundle branch");
        result[branch] = line.substr(0, split);
    }
    branches_valid(project, result);
    return result;
}
int64_t publication_time(const Document &manifest) {
    // bundle_manifest has already validated and normalized this DateTimeOffset value.
    auto date = manifest.at("createdAt").get<std::string>();
    auto day = std::chrono::year(std::stoi(date.substr(0, 4))) /
               std::chrono::month(std::stoi(date.substr(5, 2))) /
               std::chrono::day(std::stoi(date.substr(8, 2)));
    auto seconds = std::chrono::sys_days(day).time_since_epoch().count() * int64_t(86400) +
                   std::stoi(date.substr(11, 2)) * 3600 + std::stoi(date.substr(14, 2)) * 60 +
                   std::stoi(date.substr(17, 2));
    auto zone = date.find_first_of("+-", 19);
    auto offset =
        std::stoi(date.substr(zone + 1, 2)) * 3600 + std::stoi(date.substr(zone + 4, 2)) * 60;
    seconds -= date[zone] == '+' ? offset : -offset;
    std::string fraction = date[19] == '.' ? date.substr(20, zone - 20) : "";
    fraction.append(7 - fraction.size(), '0');
    return seconds * 10000000 + std::stoll(fraction);
}
bool newer(const Document &candidate, const Document &previous) {
    auto sequence = candidate.value("sequence", int64_t(0)),
         old = previous.value("sequence", int64_t(0));
    return sequence || old ? sequence > old
                           : publication_time(candidate) > publication_time(previous);
}
Document publications(const fs::path &home, const fs::path &exchange, const Json &project,
                      const Document &config, Json &rejected) {
    Document result = Document::array();
    auto transactions = directory(exchange, "transactions", false);
    if (!fs::exists(transactions))
        return result;
    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(transactions)) {
        if (++count > publication_limit)
            throw std::runtime_error(
                "CyRevision exchange exceeds 4096 publications; archive old transactions");
        const char *reason = "Incomplete or invalid publication metadata";
        try {
            if (path_is_link(entry.path()) || !entry.is_directory())
                continue;
            auto document = read_json(project_child(entry.path(), "transaction.json"));
            auto manifest = cr::bundle_manifest(document.at("manifest"));
            if (manifest.at("projectId") != config.at("project_id"))
                continue;
            if (compact(manifest.at("transactionId").get<std::string>()) !=
                path_string(entry.path().filename()))
                throw std::runtime_error(
                    "CyRevision transaction directory differs from its signed ID");
            reason = "Publishing device is not trusted or its identity changed";
            auto key = pinned_key(config, manifest.at("author"));
            reason = "Publication signature did not verify";
            if (!cr::verify_bundle(document, key, home))
                throw std::runtime_error("CyRevision bundle signature is invalid");
            reason = "Publication contains an invalid Git branch";
            branches_valid(project, manifest.at("branches"));
            result.push_back(manifest);
        } catch (const std::exception &) {
            // Report bounded diagnostics, never parse errors containing untrusted/private input.
            rejected.push_back(
                {{"directory", path_string(entry.path().filename())}, {"reason", reason}});
        }
    }
    std::sort(result.begin(), result.end(), [](const Document &a, const Document &b) {
        auto left = a.at("author").at("deviceId"), right = b.at("author").at("deviceId");
        return left != right ? left.get<std::string>() < right.get<std::string>() : newer(a, b);
    });
    return result;
}
std::map<std::string, Json> required_lfs(const Json &project, const Document &branches) {
    std::map<std::string, Json> result;
    uintmax_t bytes = 0;
    for (const auto &[name, head] : branches.items()) {
        auto inventory = lfs_inventory(project, head.get<std::string>());
        for (const auto &object : inventory.at("objects")) {
            auto oid = object.at("sha256").get<std::string>();
            auto [it, added] = result.emplace(oid, object);
            if (!added && it->second != object)
                throw std::runtime_error("Inconsistent LFS object sizes between branches");
            if (added)
                bytes += object.at("size").get<uintmax_t>();
            if (result.size() > 4096 || bytes > bundle_limit)
                throw std::runtime_error("CyRevision LFS export exceeds 4096 objects or 16 GiB");
        }
    }
    return result;
}
std::map<std::string, Document> current_lfs(const Json &project, const Document &branches) {
    std::map<std::string, Document> result;
    for (const auto &[branch, head] : branches.items()) {
        auto raw = offline_git(project, {"ls-tree", "-r", "-z", "--long", head.get<std::string>()});
        size_t start = 0, count = 0;
        while (start < raw.size()) {
            auto end = raw.find('\0', start), tab = raw.find('\t', start);
            if (end == std::string::npos || tab == std::string::npos || tab > end ||
                ++count > 100000)
                throw std::runtime_error("CyRevision current tree exceeds its inspection limit");
            std::istringstream info(raw.substr(start, tab - start));
            std::string mode, type, oid, size;
            info >> mode >> type >> oid >> size;
            if (type == "blob" && (mode == "100644" || mode == "100755") &&
                std::stoull(size) < 1024) {
                auto pointer = git_lfs_pointer(offline_git(project, {"cat-file", "blob", oid}));
                if (!pointer.is_null()) {
                    auto digest = pointer.at("sha256").get<std::string>();
                    if (!result.contains(digest))
                        result[digest] = Document::array();
                    result[digest].push_back(raw.substr(tab + 1, end - tab - 1));
                }
            }
            start = end + 1;
        }
    }
    return result;
}
Document advertised_lfs(const fs::path &home, const fs::path &exchange, const Document &config) {
    Document objects = Document::object();
    auto inventories = directory(exchange, "inventories", false);
    if (!fs::exists(inventories))
        return objects;
    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(inventories)) {
        if (++count > 1024)
            throw std::runtime_error("CyRevision exchange exceeds 1024 inventories");
        if (entry.path().extension() != ".json")
            continue;
        try {
            auto document = read_json(entry.path());
            auto inventory = cr::content_inventory(document.at("inventory"));
            if (inventory.at("projectId") != config.at("project_id") ||
                path_string(entry.path().stem()) !=
                    compact(inventory.at("device").at("deviceId").get<std::string>()))
                continue;
            auto key = pinned_key(config, inventory.at("device"));
            if (!cr::verify_inventory(document, key, home))
                continue;
            for (const auto &object : inventory.at("lfsObjects"))
                if (object.at("publishedToExchange") == true) {
                    auto oid = object.at("oidSha256").get<std::string>();
                    if (objects.contains(oid) && objects.at(oid) != object.at("size"))
                        throw std::runtime_error(
                            "Trusted inventories disagree on an LFS object size");
                    objects[oid] = object.at("size");
                }
        } catch (const std::exception
                     &) { /* Incomplete or unauthorized inventory is not a transfer grant. */
        }
    }
    return objects;
}
struct Workspace {
    fs::path path;
    explicit Workspace(const fs::path &home) : path(home / ("work-" + random_id())) {
        if (!fs::create_directory(path))
            throw std::runtime_error("Cannot reserve private CyRevision workspace");
        secure_state_path(path, true);
    }
    ~Workspace() {
        std::error_code ignored;
        fs::remove(path / "repository.bundle", ignored);
        fs::remove(path, ignored);
    }
};
void snapshot_bundle(const fs::path &source, const fs::path &destination) {
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary);
    if (!input || !output)
        throw std::runtime_error("Cannot open the CyRevision bundle snapshot");
    std::vector<char> buffer(1024 * 1024);
    uintmax_t total = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(std::min<uintmax_t>(
                                      buffer.size(), bundle_limit - total + 1)));
        auto count = input.gcount();
        if (total + count > bundle_limit)
            throw std::runtime_error("CyRevision bundle grew beyond 16 GiB while copying");
        total += count;
        output.write(buffer.data(), count);
        if (!output)
            throw std::runtime_error("Cannot write the private CyRevision bundle snapshot");
    }
    output.flush();
    if (!input.eof() || !output)
        throw std::runtime_error("CyRevision bundle copy was interrupted");
}
Json import_lfs(const fs::path &home, const fs::path &exchange, const Json &project,
                const Document &config, const Document &branches) {
    size_t copied = 0, deferred = 0;
    if (project.value("git_lfs", true)) {
        auto required = required_lfs(project, branches);
        auto grants = advertised_lfs(home, exchange, config);
        auto source_store = directory(exchange, "lfs/objects", false),
             local_store = lfs_local_store(project);
        for (const auto &[oid, object] : required) {
            auto local = lfs_object_path(local_store, oid);
            if (fs::exists(local)) {
                verify_lfs_object(local, object);
                continue;
            }
            if (!grants.contains(oid) || grants.at(oid) != object.at("size").get<uint64_t>()) {
                ++deferred;
                continue;
            }
            auto source_object = lfs_object_path(source_store, oid);
            if (!fs::exists(source_object)) {
                ++deferred;
                continue;
            }
            copy_lfs_object(source_object, local_store, object);
            ++copied;
        }
    }
    return {{"lfs_copied", copied}, {"lfs_deferred", deferred}};
}
Document review_refs(const Json &project, const std::string &device) {
    auto prefix = "refs/remotes/cysynck/cr-" + compact(device) + "/";
    std::map<std::string, std::string> actual;
    std::istringstream references(offline_git(
        project, {"for-each-ref", "--format=%(refname) %(objectname) %(symref)", prefix}));
    std::string line;
    while (std::getline(references, line)) {
        line = trim(line);
        auto split = line.find(' ');
        if (split == std::string::npos || !line.starts_with(prefix))
            throw std::runtime_error("Invalid local peer reference");
        if (line.find(' ', split + 1) != std::string::npos)
            throw std::runtime_error(
                "CyRevision review references must not be symbolic references");
        actual.emplace(line.substr(prefix.size(), split - prefix.size()), line.substr(split + 1));
    }
    return Document(actual);
}
Json recover_import(const fs::path &home, const Json &project, const Json &data) {
    auto device = cr::normalize_guid(data.at("device").get<std::string>());
    auto pending = project_child(home, "pending-" + compact(device) + ".json");
    if (!fs::exists(pending))
        return {{"pending", false}, {"applied", false}};
    auto intent = read_json(pending);
    auto manifest = cr::bundle_manifest(intent.at("manifest"));
    if (manifest.at("author").at("deviceId") != device)
        throw std::runtime_error("Invalid CyRevision pending import identity");
    auto actual = review_refs(project, device);
    bool committed = Json::parse(actual.dump()) == Json::parse(manifest.at("branches").dump());
    bool unchanged = Json::parse(actual.dump()) == Json::parse(intent.at("previous").dump());
    Json result = {{"pending", true},
                   {"can_recover", committed || unchanged},
                   {"applied", false},
                   {"outcome", committed   ? "committed"
                               : unchanged ? "not_committed"
                                           : "references_changed"},
                   {"working_tree_changed", false},
                   {"plan", sha256(intent.dump() + actual.dump())}};
    if (data.value("apply", false)) {
        if (!result.at("can_recover").get<bool>() ||
            data.value("plan", "") != result.at("plan").get<std::string>())
            throw std::runtime_error("CyRevision import recovery needs its current preview token "
                                     "and unchanged review references");
        if (committed)
            atomic_write(project_child(home, "import-" + compact(device) + ".json"),
                         Document{{"manifest", manifest}, {"recovered", true}}.dump(2), true);
        fs::remove(pending);
        result["applied"] = true;
    }
    return result;
}
Json publish(const fs::path &home, const fs::path &project_home, const fs::path &exchange,
             const Json &project, const Document &config) {
    auto author = local_identity(home, {}, false);
    auto key = project_child(home, "signing.pem");
    Workspace work(project_home);
    auto bundle = work.path / "repository.bundle";
    offline_git(project, {"bundle", "create", path_string(bundle), "--branches"});
    if (fs::file_size(bundle) > bundle_limit)
        throw std::runtime_error("CyRevision bundle exceeds 16 GiB");
    auto branches = bundle_heads(project, bundle);
    auto own_config = config;
    own_config["devices"][author.at("deviceId").get<std::string>()] = author;
    Json rejected = Json::array();
    auto existing = publications(project_home, exchange, project, own_config, rejected);
    int64_t sequence = 0;
    for (const auto &manifest : existing)
        if (manifest.at("author").at("deviceId") == author.at("deviceId"))
            sequence = std::max(sequence, manifest.value("sequence", int64_t(0)));
    auto checkpoint = project_child(project_home, "publication.json");
    if (fs::exists(checkpoint))
        sequence = std::max(sequence, read_json(checkpoint).at("sequence").get<int64_t>());
    if (sequence == INT64_MAX)
        throw std::runtime_error("CyRevision publication sequence exhausted");
    Document manifest = {{"transactionId", new_guid()},
                         {"projectId", config.at("project_id")},
                         {"author", author},
                         {"createdAt", timestamp()},
                         {"bundleFileName", "repository.bundle"},
                         {"bundleHash", sha256_file(bundle)},
                         {"branches", branches},
                         {"sequence", sequence + 1}};
    // Reserve the next sequence before shared publication; interrupted exports may leave gaps.
    atomic_write(checkpoint, Document{{"sequence", sequence + 1}}.dump(), true);
    auto objects = project.value("git_lfs", true) ? required_lfs(project, branches)
                                                  : std::map<std::string, Json>{};
    auto paths = project.value("git_lfs", true) ? current_lfs(project, branches)
                                                : std::map<std::string, Document>{};
    auto store = directory(exchange, "lfs/objects");
    Document advertised = Document::array();
    if (!objects.empty()) {
        auto local = lfs_local_store(project);
        for (const auto &[oid, object] : objects)
            verify_lfs_object(lfs_object_path(local, oid), object);
        for (const auto &[oid, object] : objects) {
            copy_lfs_object(lfs_object_path(local, oid), store, object);
            advertised.push_back(
                {{"oidSha256", oid},
                 {"size", object.at("size").get<uint64_t>()},
                 {"priority", paths.contains(oid) ? 0 : 3},
                 {"paths", paths.contains(oid) ? paths.at(oid) : Document::array()},
                 {"publishedToExchange", true}});
        }
    }
    bool windows = config.at("signature_format") == "windows-legacy";
    Document inventory = {{"inventoryId", new_guid()},
                          {"projectId", config.at("project_id")},
                          {"device", author},
                          {"createdAt", timestamp()},
                          {"lfsObjects", advertised}};
    auto signed_inventory = cr::signed_inventory(inventory, key, windows);
    auto signed_manifest = cr::signed_bundle(manifest, key, windows);
    auto inventories = directory(exchange, "inventories");
    atomic_write(
        project_child(inventories, compact(author.at("deviceId").get<std::string>()) + ".json"),
        signed_inventory);
    auto transactions = directory(exchange, "transactions");
    auto destination =
        project_child(transactions, compact(manifest.at("transactionId").get<std::string>()));
    if (!fs::create_directory(destination))
        throw std::runtime_error("Preserve an existing CyRevision transaction");
    auto published_bundle = project_child(destination, "repository.bundle");
    fs::copy_file(bundle, published_bundle, fs::copy_options::none);
    if (sha256_file(published_bundle) != manifest.at("bundleHash").get<std::string>())
        throw std::runtime_error("Published CyRevision bundle checksum mismatch");
    // Metadata is the completion marker, written only after bundle and LFS data exist.
    atomic_write(destination / "transaction.json", signed_manifest);
    return {{"manifest", Json::parse(manifest.dump())},
            {"lfs_objects", objects.size()},
            {"signature_format", config.at("signature_format").get<std::string>()}};
}
Json receive(const fs::path &home, const fs::path &exchange, const Json &project,
             const Document &config, const Document &manifest) {
    auto device = manifest.at("author").at("deviceId").get<std::string>();
    auto pending = project_child(home, "pending-" + compact(device) + ".json");
    if (fs::exists(pending))
        throw std::runtime_error(
            "Preview CyRevision recover for this publishing device before importing again");
    auto receipt_path = project_child(home, "import-" + compact(device) + ".json");
    Document previous;
    if (fs::exists(receipt_path)) {
        previous = read_json(receipt_path);
        if (!newer(manifest, previous.at("manifest"))) {
            if (manifest == previous.at("manifest")) {
                if (Json::parse(review_refs(project, device).dump()) !=
                    Json::parse(manifest.at("branches").dump()))
                    throw std::runtime_error(
                        "CyRevision review references changed since the last import");
                auto result = import_lfs(home, exchange, project, config, manifest.at("branches"));
                result["imported"] = false;
                result["already_imported"] = true;
                result["working_tree_changed"] = false;
                return result;
            }
            throw std::runtime_error("Refuse a stale or conflicting CyRevision publication");
        }
    }
    auto peer = "cr-" + compact(device), prefix = "refs/remotes/cysynck/" + peer + "/";
    auto actual = review_refs(project, device).get<std::map<std::string, std::string>>();
    Document expected =
        previous.is_null() ? Document::object() : previous.at("manifest").at("branches");
    if (Json(actual) != Json::parse(expected.dump()))
        throw std::runtime_error("CyRevision review references changed outside this adapter; "
                                 "preserve them and review the local state");
    Workspace work(home);
    auto snapshot = work.path / "repository.bundle";
    auto source = project_child(
        exchange, "transactions/" + compact(manifest.at("transactionId").get<std::string>()) +
                      "/repository.bundle");
    if (!fs::is_regular_file(source) || path_is_link(source) ||
        fs::file_size(source) > bundle_limit)
        throw std::runtime_error("CyRevision bundle is missing, linked or exceeds 16 GiB");
    snapshot_bundle(source, snapshot);
    secure_state_path(snapshot, false);
    if (fs::file_size(snapshot) > bundle_limit ||
        sha256_file(snapshot) != manifest.at("bundleHash").get<std::string>())
        throw std::runtime_error("CyRevision bundle checksum mismatch");
    offline_git(project, {"bundle", "verify", path_string(snapshot)});
    if (Json::parse(bundle_heads(project, snapshot).dump()) !=
        Json::parse(manifest.at("branches").dump()))
        throw std::runtime_error("CyRevision bundle branches differ from signed metadata");
    // Add immutable objects before inspecting pointers; HEAD, index and worktree stay unchanged.
    offline_git(project, {"bundle", "unbundle", path_string(snapshot)});
    for (const auto &[branch, head] : manifest.at("branches").items())
        if (trim(offline_git(project, {"cat-file", "-t", head.get<std::string>()})) != "commit")
            throw std::runtime_error("CyRevision branch does not identify a Git commit");
    auto lfs = import_lfs(home, exchange, project, config, manifest.at("branches"));
    std::string commands = "start\n";
    for (const auto &[name, head] : manifest.at("branches").items()) {
        commands += "update " + prefix + name + " " + head.get<std::string>() + " " +
                    (actual.contains(name) ? actual.at(name)
                                           : std::string(head.get<std::string>().size(), '0')) +
                    "\n";
    }
    for (const auto &[name, head] : actual)
        if (!manifest.at("branches").contains(name))
            commands += "delete " + prefix + name + " " + head + "\n";
    commands += "prepare\ncommit\n";
    // A durable intent enables diagnosis if the process dies after Git commits its ref transaction.
    atomic_write(pending, Document{{"manifest", manifest}, {"previous", expected}}.dump(2), true);
    auto changed = offline_git_result(project, {"update-ref", "--no-deref", "--stdin"}, commands);
    if (changed.code) {
        fs::remove(pending);
        throw std::runtime_error(
            "CyRevision review references changed during import; no ref transaction was committed");
    }
    atomic_write(receipt_path,
                 Document{{"manifest", manifest},
                          {"lfs_copied", lfs.at("lfs_copied").get<size_t>()},
                          {"lfs_deferred", lfs.at("lfs_deferred").get<size_t>()}}
                     .dump(2),
                 true);
    fs::remove(pending);
    return {{"imported", true},
            {"peer", peer},
            {"branches", Json::parse(manifest.at("branches").dump())},
            {"lfs_copied", lfs.at("lfs_copied")},
            {"lfs_deferred", lfs.at("lfs_deferred")},
            {"working_tree_changed", false}};
}
} // namespace

Json cyrevision_action(const fs::path &state, const Json &project, const std::string &action,
                       const Json &data) {
    auto home = directory(state, "cyrevision");
    if (project.is_null()) {
        if (action != "identity")
            throw std::runtime_error("Unknown CyRevision identity action");
        return public_identity(local_identity(home, data, data.contains("name")));
    }
    if (project.at("mode") != "git-bundles")
        throw std::runtime_error("CyRevision requires a Git exchange project");
    auto project_home = directory(home, "projects/" + project.at("id").get<std::string>());
    auto config = settings(project_home);
    auto settings_path = project_child(project_home, "settings.json");
    if (action == "configure") {
        auto project_id = cr::normalize_guid(data.at("project_id").get<std::string>());
        if (config.contains("project_id") && config.at("project_id") != project_id)
            throw std::runtime_error("Preserve the existing CyRevision project association; create "
                                     "a separate CySynck project");
        auto format = data.value("signature_format", "lf");
        if (format != "lf" && format != "windows-legacy")
            throw std::runtime_error("Choose CyRevision signature_format lf or windows-legacy");
        config["project_id"] = project_id;
        config["signature_format"] = format;
        atomic_write(settings_path, config.dump(2), true);
    }
    if (action == "status" || action == "configure")
        return Json::parse(config.dump());
    if (!config.contains("project_id"))
        throw std::runtime_error("Configure the CyRevision project GUID first");
    if (action == "recover")
        return recover_import(project_home, project, data);
    if (action == "trust") {
        auto candidate = cr::identity(Document::parse(data.at("identity").dump()));
        auto key = candidate.at("signingPublicKey").get<std::string>();
        if (data.at("fingerprint").get<std::string>() != cr::fingerprint(key))
            throw std::runtime_error(
                "Verify the publishing key fingerprint through a separate trusted channel");
        auto device = candidate.at("deviceId").get<std::string>();
        if (config.at("devices").contains(device) && config.at("devices").at(device) != candidate)
            throw std::runtime_error(
                "Revoke the existing pinned identity explicitly before replacing it");
        config["devices"][device] = candidate;
        atomic_write(settings_path, config.dump(2), true);
        return public_identity(candidate);
    }
    if (action == "revoke") {
        config["devices"].erase(cr::normalize_guid(data.at("device").get<std::string>()));
        atomic_write(settings_path, config.dump(2), true);
        return {{"revoked", true}, {"existing_review_references_preserved", true}};
    }
    auto exchange = fs::u8path(project.at("exchange_path").get<std::string>());
    plain_directory(exchange);
    fs::create_directories(exchange);
    if (action == "export")
        return publish(home, project_home, exchange, project, config);
    Json rejected = Json::array();
    auto incoming = publications(project_home, exchange, project, config, rejected);
    if (action == "inbox")
        return {{"publications", Json::parse(incoming.dump())}, {"rejected", rejected}};
    if (action == "import") {
        auto transaction = cr::normalize_guid(data.at("transaction").get<std::string>());
        for (const auto &manifest : incoming)
            if (manifest.at("transactionId") == transaction)
                return receive(project_home, exchange, project, config, manifest);
        throw std::runtime_error(
            "No complete, trusted CyRevision publication has this transaction ID");
    }
    throw std::runtime_error("Unknown CyRevision exchange action");
}
} // namespace cysynck
