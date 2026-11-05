#include <cysynck/core.hpp>
#include "git_lfs.hpp"
#include "git_working.hpp"
#include <algorithm>
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
static std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
        value.pop_back();
    return value;
}
static std::map<std::string, std::string> git_state(const fs::path &root) {
    std::map<std::string, std::string> result;
    for (const auto &entry : fs::recursive_directory_iterator(root / ".git"))
        if (entry.is_regular_file())
            result.emplace(path_string(fs::relative(entry.path(), root)), read_file(entry.path()));
    return result;
}
int main() {
    auto root = fs::current_path() / "git-working-data" / random_id();
    fs::create_directories(root);
    try {
        for (auto format : {"sha1", "sha256"}) {
            auto repo = root / format;
            fs::create_directories(repo);
            Json project{{"root", path_string(repo)}};
            git(project, {"init", "--object-format=" + std::string(format)});
            git(project, {"config", "core.autocrlf", "true"});
            // A configured filter must never be executed, including for CRLF and binary files.
            git(project, {"config", "filter.probe.clean", "echo invoked > filter-ran; exit 1"});
            git(project, {"config", "filter.probe.required", "true"});
            atomic_write(repo / ".gitattributes", "* filter=probe text eol=lf\n");
            Json files = Json::array();
            std::vector<std::string> reference{"hash-object", "--no-filters", "--"};
            uintmax_t total = 0;
            for (size_t i = 0; i < 260; ++i) {
                auto name = "directory with spaces/file " + std::to_string(i) + ".txt";
                std::string data = "raw bytes " + std::to_string(i) + "\r\n";
                if (i == 0) {
                    name = "-leading-dash";
                    data = "";
                }
                if (i == 1) {
                    name = "binary.dat";
                    data = std::string("binary\0\nbytes\r\n", 15);
                }
                if (i == 2)
                    name = "unicode-\xc3\xa9t\xc3\xa9.txt";
                if (i == 3 || i == 4)
                    data = "identical bytes\n";
#ifndef _WIN32
                if (i == 5)
                    name = "line\nbreak.txt";
                if (i == 6)
                    name = "tab\tand\"quote.txt";
#endif
                auto path = repo / fs::u8path(name);
                fs::create_directories(path.parent_path());
                atomic_write(path, data);
                total += data.size();
                files.push_back({{"path", name}, {"bytes", data.size()}});
                reference.push_back(path_string(path));
            }
            // Keep the independent fixture command below Windows' command-line limit too.
            std::string reference_output;
            for (size_t first = 3; first < reference.size(); first += 64) {
                std::vector<std::string> command(reference.begin(), reference.begin() + 3);
                command.insert(command.end(), reference.begin() + first,
                               reference.begin() + std::min(first + 64, reference.size()));
                reference_output += offline_git(project, command);
            }
            std::istringstream output(reference_output);
            std::map<std::string, std::string> expected;
            for (auto &file : files) {
                std::string id;
                std::getline(output, id);
                file["oid"] = trim(id);
                expected.emplace(file.at("path").get<std::string>(), trim(id));
            }
            auto before = git_state(repo);
            auto batch = git_working_hashes(project, files);
            check(batch.ids == expected,
                  "Batches changed raw bytes, ordering or binary identities");
            check(batch.batches > 1 && batch.batches < 10, "260 files did not use bounded batches");
            check(batch.bytes == total, "Inspected byte count differs");
            check(!fs::exists(repo / "filter-ran"), "Raw working inspection executed a filter");
            check(git_state(repo) == before,
                  "Inspection wrote Git objects, index or configuration");
            auto reversed = files;
            std::reverse(reversed.begin(), reversed.end());
            check(git_working_hashes(project, reversed).ids == expected,
                  "Reordering changed identities");
            auto empty = git_working_hashes(project, Json::array());
            check(empty.ids.empty() && empty.batches == 0 && empty.bytes == 0,
                  "Empty input ran Git");

            // A same-size edit with restored timestamps must still be detected by hashing bytes.
            auto edited = repo / "directory with spaces/file 10.txt";
            auto timestamp = fs::last_write_time(edited);
            auto original = read_file(edited);
            auto replacement = original;
            replacement[0] = 'X';
            atomic_write(edited, replacement);
            fs::last_write_time(edited, timestamp);
            auto changed = git_working_hashes(project, files);
            check(changed.ids.at("directory with spaces/file 10.txt") !=
                      expected.at("directory with spaces/file 10.txt"),
                  "Same-size edit with restored mtime escaped content verification");
            atomic_write(edited, original);
            fs::last_write_time(edited, timestamp);

            auto invalid = files.at(1);
            for (auto path : {"../outside", ".git/config", "binary.dat/../binary.dat", "abs:bad"}) {
                invalid["path"] = path;
                rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                        "Unsafe path accepted");
            }
            invalid = files.at(1);
            for (auto size : {Json(-1), Json(1.5), Json(1024ull * 1024 * 1024 + 1), Json(1)}) {
                invalid["bytes"] = size;
                rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                        "Invalid file size accepted");
            }
            invalid = files.at(1);
            invalid["oid"] = "bad\nHEAD";
            rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                    "Invalid identity accepted");
            invalid["oid"] = std::string(std::string(format) == "sha1" ? 64 : 40, '0');
            rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                    "Wrong object format accepted");
            rejects([&] { git_working_hashes(project, Json::array({files[1], files[1]})); },
                    "Duplicate accepted");
            invalid = files.at(1);
            invalid["path"] = "BINARY.dat";
            rejects([&] { git_working_hashes(project, Json::array({files[1], invalid})); },
                    "Case alias accepted");
            for (auto path : {"absent", "directory with spaces"}) {
                invalid = files.at(0);
                invalid["path"] = path;
                rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                        "Non-file accepted");
            }
            for (auto value : {Json(nullptr), Json::object(), Json::array({Json::object()})})
                rejects([&] { git_working_hashes(project, value); }, "Invalid inventory accepted");
            Json too_many = Json::array();
            for (size_t i = 0; i < 10001; ++i)
                too_many.push_back(files[0]);
            rejects([&] { git_working_hashes(project, too_many); }, "Oversized inventory accepted");
            std::error_code link_error;
            fs::create_symlink(repo / "binary.dat", repo / "linked.dat", link_error);
            if (!link_error) {
                invalid = files.at(1);
                invalid["path"] = "linked.dat";
                rejects([&] { git_working_hashes(project, Json::array({invalid})); },
                        "Linked file accepted");
            } else
                std::cout << "Symlink fixture unavailable: " << link_error.message() << '\n';

            // A file above the aggregate batch budget is inspected alone, without loading it in
            // RAM.
            atomic_write(repo / "large.bin", "large fixture");
            fs::resize_file(repo / "large.bin", 33ull * 1024 * 1024);
            auto large_id = trim(offline_git(
                project, {"hash-object", "--no-filters", "--", path_string(repo / "large.bin")}));
            auto mixed = Json::array({files[1],
                                      {{"path", "large.bin"},
                                       {"bytes", fs::file_size(repo / "large.bin")},
                                       {"oid", large_id}},
                                      files[2]});
            auto large_batch = git_working_hashes(project, mixed);
            check(large_batch.ids.at("large.bin") == large_id && large_batch.batches == 3,
                  "Large raw working file was not isolated or hashed correctly");

            Json sample = Json::array();
            for (size_t i = 0; i < 32; ++i)
                sample.push_back(files[i]);
            auto start = std::chrono::steady_clock::now();
            auto sample_batch = git_working_hashes(project, sample);
            auto batched = std::chrono::steady_clock::now() - start;
            start = std::chrono::steady_clock::now();
            for (const auto &file : sample) {
                auto name = file.at("path").get<std::string>();
                check(trim(offline_git(project, {"hash-object", "--no-filters", "--",
                                                 path_string(repo / fs::u8path(name))})) ==
                          sample_batch.ids.at(name),
                      "Individual hash differs from the batch");
            }
            auto individual = std::chrono::steady_clock::now() - start;
            std::cout << format << ": 260 files in " << batch.batches
                      << " Git processes; 32-file sample: individual "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(individual).count()
                      << " ms; batched "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(batched).count()
                      << " ms\n";
            check(git_state(repo) == before, "Final inspection changed the Git repository");
            check(!fs::exists(repo / "filter-ran"), "A later inspection executed the clean filter");
        }
        std::cout << "PASS " << checks << " batched Git working file checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
