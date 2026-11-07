#include "help.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void set_root(const std::string &value) {
#ifdef _WIN32
    _putenv_s("CYSYNCK_DOC_ROOT", value.c_str());
#else
    if (value.empty())
        unsetenv("CYSYNCK_DOC_ROOT");
    else
        setenv("CYSYNCK_DOC_ROOT", value.c_str(), 1);
#endif
}
} // namespace
int main(int argc, char **argv) {
    using namespace cysynck;
    const auto previous = env("CYSYNCK_DOC_ROOT");
    auto fixture = fs::current_path() / fs::u8path("help-test-data/café space % #");
    try {
        require(argc == 2, "Expected a complete documentation directory");
        fs::create_directories(fixture);
        // Fresh owned files; never modify a user profile or launch a browser in this test.
        fs::copy(fs::u8path(argv[1]), fixture,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        set_root(path_string(fixture));
        auto target = help::location("backups", "verifier-et-restaurer", "fr");
        auto url = target.at("url").get<std::string>();
        require(url.starts_with("file:///"), "Expected a file URL");
        require(url.find("caf%C3%A9%20space%20%25%20%23") != std::string::npos,
                "UTF-8/reserved path bytes must be encoded");
        require(url.ends_with("#/fr/backups/verifier-et-restaurer"), "Contextual route lost");
        require(help::location("docs/git-cache.md", {}, "fr").at("page") == "git-cache",
                "Legacy Markdown path lost");
        require(help::location("roadmap").at("page") == "availability", "Legacy alias lost");
        require(help::location("backups", {}, "de").at("language") == "en",
                "Unsupported locale must fall back to English");
        require(help::context("Backups", "fr").at("section") == "verifier-et-restaurer",
                "Native context lost");
        require(help::context("unrecognized view").at("page") == "overview",
                "Unknown view must use the overview");
        auto printed = help::command({"docs", "git-asset-export", "--language", "fr", "--print"});
        require(!printed.at("opened").get<bool>(), "Print-only must never launch a browser");
        for (const auto &args : std::vector<std::vector<std::string>>{
                 {"docs", "unknown", "--print"},
                 {"docs", "backups", "--section", "missing", "--print"},
                 {"docs", "--unknown"},
                 {"docs", "--language"},
                 {"docs", "overview", "extra"}}) {
            bool rejected = false;
            try {
                (void)help::command(args);
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "Invalid page/section/options must be refused");
        }
        auto special = help::file_url(fixture / fs::u8path("test?#é.html"));
        require(special.ends_with("test%3F%23%C3%A9.html"),
                "Reserved URL characters escaped incorrectly");
        set_root(path_string(fixture / "absent"));
        bool incomplete = false;
        try {
            (void)help::directory();
        } catch (const std::exception &) {
            incomplete = true;
        }
        require(incomplete, "Incomplete exports must be refused");
        set_root(previous);
        std::cout << "Offline help: contextual routes, aliases, locale fallback, print-only and "
                     "Unicode paths passed\n";
        return 0;
    } catch (const std::exception &error) {
        set_root(previous);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
