#include <cysynck/core.hpp>
#include "git_blobs.hpp"
#include "git_lfs.hpp"
#include <functional>
#include <iostream>
#include <sstream>
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
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}
int main() {
    auto root = fs::current_path() / "git-blob-data" / random_id();
    fs::create_directories(root);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto repo = root / format;
            fs::create_directories(repo);
            Json p{{"root", path_string(repo)}};
            git(p, {"init", "--object-format=" + std::string(format)});
            Json entries = Json::array();
            std::map<std::string, std::string> expected;
            std::string inputs;
            for (size_t i = 0; i < 260; ++i) {
                std::string text = "blob " + std::to_string(i) + "\n";
                if (i == 0)
                    text = "";
                if (i == 1)
                    text = std::string("binary\0\nbytes\r\n", 15);
                if (i == 2)
                    text = std::string(1023, 'x');
                auto name = "file" + std::to_string(i);
                atomic_write(repo / name, text);
                inputs += name + '\n';
            }
            // Store and enumerate all test blobs with one Git process, preserving binary data.
            auto raw =
                offline_git(p, {"hash-object", "--no-filters", "-w", "--stdin-paths"}, inputs);
            std::istringstream ids(raw);
            for (size_t i = 0; i < 260; ++i) {
                std::string id;
                std::getline(ids, id);
                auto text = read_file(repo / ("file" + std::to_string(i)));
                expected.emplace(id, text);
                entries.push_back({{"oid", id}, {"bytes", text.size()}});
            }
            entries.push_back(entries.at(1));
            auto start = std::chrono::steady_clock::now();
            auto batch = git_small_blobs(p, entries);
            auto batched = std::chrono::steady_clock::now() - start;
            check(batch == expected,
                  "Multiple batches preserve empty, binary, 1023-byte and duplicated blobs");
            start = std::chrono::steady_clock::now();
            for (const auto &[id, text] : expected)
                check(offline_git(p, {"cat-file", "blob", id}) == text,
                      "Batch differs from individual raw Git reads");
            auto individual = std::chrono::steady_clock::now() - start;
            std::cout << format << ": 260 individual reads "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(individual).count()
                      << " ms; batched "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(batched).count()
                      << " ms\n";
            check(git_small_blobs(p, Json::array()).empty(),
                  "Empty inventory should need no objects");
            auto large = entries.at(0);
            large["bytes"] = 1024;
            check(git_small_blobs(p, Json::array({large})).empty(),
                  "1024-byte entries are not small blobs");
            for (auto value : {Json(nullptr), Json::object(),
                               Json::array({{{"oid", "bad\nHEAD"}, {"bytes", 1}}}),
                               Json::array({{{"oid", entries[0]["oid"]}, {"bytes", -1}}})})
                rejects([&] { git_small_blobs(p, value); }, "Invalid inventory accepted");
            auto wrong = entries.at(2);
            wrong["bytes"] = 1022;
            rejects([&] { git_small_blobs(p, Json::array({wrong})); }, "Wrong blob size accepted");
            rejects([&] { git_small_blobs(p, Json::array({wrong, entries.at(2)})); },
                    "Duplicate inconsistent size accepted");
            auto absent = entries.at(0);
            absent["oid"] = std::string(format == std::string("sha1") ? 40 : 64, '0');
            rejects([&] { git_small_blobs(p, Json::array({absent})); }, "Missing object accepted");
        }
        std::cout << "PASS " << checks << " batched Git blob checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
