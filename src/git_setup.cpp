#include "git_setup.hpp"
#include "git_lfs.hpp"
#include "backup.hpp"
#include <stdexcept>

namespace cysynck {
Json setup_git_repository(const Json &project, const Json &data) {
    if (project.at("mode") != "git-bundles")
        throw std::runtime_error("Git setup requires git-bundles mode");
    auto branch = data.value("branch", std::string("main"));
    auto format = data.value("object_format", std::string("sha1"));
    if (branch.empty() || branch.size() > 1024 || branch.front() == '-' || branch == "HEAD" ||
        branch.find('\0') != branch.npos || (format != "sha1" && format != "sha256"))
        throw std::runtime_error("Choose a valid initial branch and sha1 or sha256 object format");
    auto root = fs::absolute(fs::u8path(project.at("root").get<std::string>()));
    fs::path current;
    for (const auto &part : root) {
        current /= part;
        plain_directory(current);
    }
    if (!fs::is_directory(root))
        throw std::runtime_error("Project directory is missing");
    auto metadata = root / ".git";
    std::error_code error;
    auto status = fs::symlink_status(metadata, error);
    if (error && error != std::errc::no_such_file_or_directory)
        throw std::runtime_error("Cannot inspect project Git metadata");
    bool present = !error && status.type() != fs::file_type::not_found;
    bool bare = fs::exists(root / "HEAD") && fs::is_directory(root / "objects") &&
                fs::exists(root / "config");
    Json result = {{"root", path_string(root)},
                   {"branch", branch},
                   {"object_format", format},
                   {"metadata_present", present},
                   {"can_initialize", !present && !bare},
                   {"applied", false},
                   {"plan", nullptr},
                   {"reason", present ? "Project Git metadata already exists"
                              : bare  ? "Project folder is a bare Git repository"
                                      : ""}};
    if (!present && !bare) {
        require_default_git_environment();
        offline_git(project, {"check-ref-format", "refs/heads/" + branch});
        result["plan"] = sha256(Json{{"project", project.at("id")},
                                     {"root", path_string(fs::canonical(root))},
                                     {"branch", branch},
                                     {"object_format", format},
                                     {"metadata", "absent"}}
                                    .dump());
    }
    if (!data.value("apply", false))
        return result;
    if (!result.at("can_initialize").get<bool>())
        throw std::runtime_error("Git metadata already exists; initialization refused");
    if (data.value("plan", std::string{}) != result.at("plan").get<std::string>())
        throw std::runtime_error("Git setup plan changed or is missing; preview setup again");
    // Reserve only a new metadata directory. Never reinitialize an existing repository.
    if (!fs::create_directory(metadata))
        throw std::runtime_error("Git metadata appeared during setup; initialization refused");
    try {
        project_child(root, ".git");
        offline_git(project, {"init", "--template=", "--initial-branch=" + branch,
                              "--object-format=" + format});
        auto gitdir = offline_git(project, {"rev-parse", "--absolute-git-dir"});
        while (!gitdir.empty() && (gitdir.back() == '\n' || gitdir.back() == '\r'))
            gitdir.pop_back();
        auto head = offline_git(project, {"symbolic-ref", "HEAD"});
        while (!head.empty() && (head.back() == '\n' || head.back() == '\r'))
            head.pop_back();
        if (!fs::equivalent(fs::u8path(gitdir), metadata) || head != "refs/heads/" + branch ||
            offline_git_result(project, {"rev-parse", "--verify", "--quiet", "HEAD"}).code != 1)
            throw std::runtime_error("Initialized repository identity mismatch");
    } catch (const std::exception &) {
        throw std::runtime_error(
            "Git initialization failed; newly created metadata was preserved for inspection");
    }
    result.update({{"metadata_present", true},
                   {"can_initialize", false},
                   {"applied", true},
                   {"plan", nullptr},
                   {"reason", ""}});
    return result;
}
} // namespace cysynck
