#include <cysynck/core.hpp>
#include "backup_data.hpp"
#include <functional>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &call, const char *message) {
    try {
        call();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
static Json act(Core &core, const std::string &action, const Json &data = Json::object()) {
    return core.request("POST", "/api/projects/cdc/" + action, data);
}
static Json manifest(Core &core, const Json &snapshot) {
    return Json::parse(
        read_file(core.state() / "snapshots/cdc" / (snapshot.at("id").get<std::string>() + ".json"),
                  64 * 1024 * 1024));
}
int main() {
    auto run = fs::current_path() / "cdc-backup-data" / random_id();
    fs::create_directories(run);
    try {
        Core core(run / "state");
        auto source = run / fs::u8path("source été");
        fs::create_directories(source);
        auto project =
            cli(core, {"project", "add", "Content backup", path_string(source), "--id", "cdc",
                       "--mode", "backup", "--storage", "cdc", "--interval", "0", "--keep", "10"});
        check(project.at("backup_storage") == "cdc",
              "CLI creation accepts the content-defined policy");
        rejects([&] { cli(core, {"project", "set", "cdc", "--storage", "unknown"}); },
                "Unknown storage algorithms are refused");
        std::string original(24 * 1024 * 1024 + 97, '\0');
        uint64_t random = 0x123456789abcdef0ull;
        for (auto &byte : original) {
            random ^= random << 13;
            random ^= random >> 7;
            random ^= random << 17;
            byte = static_cast<char>(random >> 56);
        }
        auto file = source / fs::u8path("asset été.bin");
        atomic_write(file, original);
        atomic_write(source / "empty.txt", "");
        atomic_write(source / "small.txt", "small\n");
        auto first = act(core, "backup");
        auto initial = manifest(core, first);
        auto recipe = initial.at("files").at("asset été.bin");
        check(recipe.at("storage") == "blocks-cdc-v1" &&
                  first.at("block_storage").at("content_defined_files") == 1,
              "Content-defined capture publishes its versioned recipe and count");
        check(!initial.at("files").at("empty.txt").contains("blocks") &&
                  !initial.at("files").at("small.txt").contains("blocks"),
              "Small and empty files keep whole-file storage");
        auto references = backup_references(recipe);
        uintmax_t total = 0;
        bool bounded = true, variable = false;
        Json lengths = Json::array();
        for (size_t i = 0; i < references.size(); ++i) {
            auto length = references.at(i).at("size").get<uintmax_t>();
            total += length;
            bounded &= length > 0 && length <= backup_cdc_max &&
                       (i + 1 == references.size() || length >= backup_cdc_min);
            variable |= i + 1 < references.size() && length != backup_block_size;
            lengths.push_back(length);
        }
        check(lengths ==
                  Json::array({1159619, 4298682, 4408326, 8388608, 4778190, 1242700, 889796}),
              "gear64-v1 boundaries match the versioned binary fixture");
        check(bounded && variable && total == original.size(),
              "Variable block sizes cover every byte within format bounds");
        check(act(core, "verify", {{"snapshot", first.at("id")}}).at("ok"),
              "Initial content-defined capture verifies");
        auto repeated = act(core, "backup");
        check(repeated.at("block_storage").at("new_block_bytes") == 0 &&
                  repeated.at("block_storage").at("reused_block_bytes") == original.size() &&
                  manifest(core, repeated).at("files").at("asset été.bin") == recipe,
              "Unchanged content has deterministic boundaries and reuses all blocks");
        auto prefix = std::string("inserted prefix\0with binary data\xff", 33);
        auto changed = prefix + original;
        atomic_write(file, changed);
        auto inserted = act(core, "backup");
        auto reused = inserted.at("block_storage").at("reused_block_bytes").get<uintmax_t>();
        check(reused >= 8 * 1024 * 1024 &&
                  inserted.at("block_storage").at("new_block_bytes").get<uintmax_t>() <
                      changed.size(),
              "A prefix insertion recovers unchanged content boundaries and block reuse");
        auto removed = changed.substr(4096);
        atomic_write(file, removed);
        auto deleted = act(core, "backup");
        check(deleted.at("block_storage").at("reused_block_bytes").get<uintmax_t>() >=
                  8 * 1024 * 1024,
              "A prefix deletion also recovers unchanged boundaries");
        for (const auto &[snapshot, bytes] : std::vector<std::pair<Json, std::string>>{
                 {first, original}, {inserted, changed}, {deleted, removed}}) {
            auto destination = run / ("restored-" + snapshot.at("id").get<std::string>());
            act(core, "restore",
                {{"snapshot", snapshot.at("id")}, {"destination", path_string(destination)}});
            check(read_file(destination / fs::u8path("asset été.bin"), bytes.size()) == bytes &&
                      fs::file_size(destination / "empty.txt") == 0,
                  "Old and edited content-defined snapshots restore exact binary bytes and Unicode "
                  "paths");
        }
        auto diff = act(core, "diff", {{"snapshot", first.at("id")}, {"path", "asset été.bin"}});
        check(diff.at("storage") == "blocks-cdc-v1" &&
                  diff.at("before_info").at("expected_sha256") == recipe.at("sha256"),
              "Bounded diff reports the correct content-defined storage and expected digest");
        auto catalog = core.request("GET", "/api/storage");
        check(catalog.at("content_defined_files") == 4 && catalog.at("can_prune") &&
                  catalog.at("deduplicated_bytes").get<uintmax_t>() >= reused,
              "The storage catalog includes content-defined references and savings");
        auto damaged = core.state() / "blocks" / references.at(0).at("sha256").get<std::string>();
        auto authentic = read_file(damaged, backup_cdc_max);
        atomic_write(damaged, std::string(authentic.size(), 'x'), true);
        check(!act(core, "verify", {{"snapshot", first.at("id")}}).at("ok").get<bool>(),
              "Same-sized corrupt CDC objects fail SHA-256 verification");
        auto rejectedDestination = run / "corrupt-restore";
        rejects(
            [&] {
                act(core, "restore",
                    {{"snapshot", first.at("id")},
                     {"destination", path_string(rejectedDestination)}});
            },
            "Corrupt content-defined snapshots cannot restore");
        check(!fs::exists(rejectedDestination),
              "Failed verification does not create the restore destination");
        atomic_write(file, original);
        act(core, "backup");
        check(act(core, "verify", {{"snapshot", first.at("id")}}).at("ok"),
              "Capturing authentic source bytes repairs shared damaged blocks");
        auto manifestPath =
            core.state() / "snapshots/cdc" / (first.at("id").get<std::string>() + ".json");
        auto encoded = read_file(manifestPath, 64 * 1024 * 1024);
        auto malformed = initial;
        malformed["files"]["asset été.bin"]["blocks"][0]["size"] = backup_cdc_min - 1;
        atomic_write(manifestPath, malformed.dump(), true);
        auto blocked = core.request("GET", "/api/storage");
        check(!blocked.at("can_prune").get<bool>(), "Malformed CDC recipes block cleanup");
        rejects([&] { act(core, "backup"); },
                "Malformed history prevents retention from deleting valid snapshots");
        check(fs::exists(damaged), "Blocked cleanup preserves referenced blocks");
        atomic_write(manifestPath, encoded, true);
        auto swapped = initial;
        std::swap(swapped["files"]["asset été.bin"]["blocks"][0],
                  swapped["files"]["asset été.bin"]["blocks"][1]);
        atomic_write(manifestPath, swapped.dump(), true);
        check(!act(core, "verify", {{"snapshot", first.at("id")}}).at("ok").get<bool>(),
              "Whole-file SHA-256 catches authentic CDC blocks reordered in a valid-size recipe");
        atomic_write(manifestPath, encoded, true);
        for (auto length : {uintmax_t{0}, backup_cdc_max + 1}) {
            auto invalid = recipe;
            invalid["blocks"][0]["size"] = length;
            rejects([&] { backup_references(invalid); },
                    "CDC rejects zero or oversized block lengths");
        }
        auto wrongTotal = recipe;
        wrongTotal["size"] = original.size() + 1;
        rejects([&] { backup_references(wrongTotal); }, "CDC rejects incomplete byte coverage");
        auto excessive = recipe;
        excessive["size"] = backup_cdc_min * backup_max_blocks + 1;
        rejects([&] { backup_references(excessive); }, "CDC enforces the 64 GiB recipe bound");
        auto missingDigest = recipe;
        missingDigest["blocks"][0]["sha256"] = "invalid";
        rejects([&] { backup_references(missingDigest); },
                "CDC rejects malformed block identities");
        cli(core, {"project", "set", "cdc", "--storage", "auto"});
        auto fixed = act(core, "backup");
        check(manifest(core, fixed).at("files").at("asset été.bin").at("storage") == "blocks-v1" &&
                  fixed.at("block_storage").at("content_defined_files") == 0,
              "Switching policy captures fixed blocks without rewriting earlier recipes");
        atomic_write(file, changed);
        auto fixedInserted = act(core, "backup");
        check(fixedInserted.at("block_storage").at("reused_block_bytes").get<uintmax_t>() < reused,
              "Content-defined capture reuses more than fixed offsets for this insertion fixture");
        cli(core, {"project", "set", "cdc", "--storage", "whole"});
        auto whole = act(core, "backup");
        check(!manifest(core, whole).at("files").at("asset été.bin").contains("blocks") &&
                  act(core, "verify", {{"snapshot", first.at("id")}}).at("ok") &&
                  act(core, "verify").at("ok"),
              "Whole, fixed and content-defined snapshots coexist and remain verifiable");
        cli(core, {"project", "set", "cdc", "--storage", "cdc", "--keep", "1"});
        atomic_write(file, std::string(9 * 1024 * 1024, 'a'));
        auto uniform = act(core, "backup");
        auto uniformRecipe = manifest(core, uniform).at("files").at("asset été.bin");
        check(uniformRecipe.at("blocks").size() == 2 &&
                  uniformRecipe.at("blocks").at(0).at("size") == backup_cdc_max,
              "Low-entropy content is cut at the hard maximum bound");
        check(core.request("GET", "/api/projects/cdc/snapshots").size() == 1,
              "Retention handles a history of mixed storage layouts");
        auto orphan = core.state() / "blocks" / sha256("unreferenced CDC object");
        atomic_write(orphan, "unreferenced CDC object", true);
        auto cleanup = core.request("GET", "/api/storage");
        core.request("POST", "/api/storage/prune", {{"apply", true}, {"plan", cleanup.at("plan")}});
        check(!fs::exists(orphan) && act(core, "verify").at("ok"),
              "Explicit cleanup removes orphaned blocks and retains all current CDC references");
        std::cout << "PASS " << checks << " content-defined backup checks; insertion reused "
                  << reused << " bytes; lengths " << lengths.dump()
                  << "; fixtures: " << path_string(run) << '\n';
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what()
                  << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
