#include <cysynck/core.hpp>
#include <tinyxml2.h>
#include <functional>
#include <iostream>
using namespace cysynck;
static int checks = 0;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
static void rejects(const std::function<void()> &fn, const char *message) {
    try {
        fn();
    } catch (const std::exception &) {
        ++checks;
        return;
    }
    throw std::runtime_error(message);
}
int main() {
    auto run = fs::current_path() / "service-test-data" / random_id();
    try {
        Core core(run / "state");
        Json data{{"platform", "linux"},
                  {"executable", "/opt/App éspace % $x/bin/cysynck"},
                  {"target_state", "/srv/Profile & éspace % $HOME 'quoted'"},
                  {"port", 8484}};
        auto plan = [&](const Json &input) {
            return core.request("POST", "/api/service/plan", input);
        };
        auto export_files = [&](Json input, const fs::path &destination) {
            input["destination"] = path_string(destination);
            return core.request("POST", "/api/service/export", input);
        };
        auto before = read_file(core.state() / "registry.json");
        auto linux_plan = plan(data);
        check(!linux_plan.at("installed") && linux_plan.at("arguments").at(5) == "127.0.0.1",
              "Service plans use loopback and do not claim installation");
        check(plan(data).at("plan") == linux_plan.at("plan"),
              "The same parameters give a stable preview");
        check(linux_plan.at("contents")
                      .get<std::string>()
                      .find("ExecStart=:\"/opt/App éspace %% $x/bin/cysynck\"") !=
                  std::string::npos,
              "Systemd disables environment expansion and escapes percent specifiers");
        check(linux_plan.at("contents").get<std::string>().find("UMask=0077") !=
                      std::string::npos &&
                  linux_plan.at("contents").get<std::string>().find("KillMode=control-group") !=
                      std::string::npos,
              "Systemd restricts file creation and stops the service process group");
        check(read_file(core.state() / "registry.json") == before,
              "Preparing a service changes neither settings nor startup registration");
        auto other = data;
        other["target_state"] = "/srv/second-profile";
        check(plan(other).at("filename") != linux_plan.at("filename"),
              "Profiles have independent service identities");
        other = data;
        other["port"] = 8585;
        check(plan(other).at("filename") == linux_plan.at("filename") &&
                  plan(other).at("plan") != linux_plan.at("plan"),
              "A port change keeps service identity but invalidates the preview");
        rejects([&] { export_files(data, run / "without-preview"); },
                "Export requires a matching preview");
        other["plan"] = linux_plan.at("plan");
        rejects([&] { export_files(other, run / "stale"); },
                "Changed parameters cannot reuse a preview");
        check(!fs::exists(run / "stale"), "A stale preview creates no output directory");
        data["plan"] = linux_plan.at("plan");
        auto result = export_files(data, run / "linux-export");
        auto filename = linux_plan.at("filename").get<std::string>();
        check(linux_plan.at("contents") == read_file(run / "linux-export" / filename) &&
                  linux_plan.at("instructions") == read_file(run / "linux-export/README.md"),
              "Export writes the reviewed configuration and target-specific instructions");
        auto receipt = Json::parse(read_file(run / "linux-export/service-export.json"));
        check(receipt.at("files").at(filename) == sha256_file(run / "linux-export" / filename) &&
                  !result.at("installed"),
              "Export receipts verify generated files without claiming activation");
        rejects([&] { export_files(data, run / "linux-export"); },
                "Existing exports are preserved");
        rejects([&] { export_files(data, core.state() / "export"); },
                "Export cannot write inside a live state profile");
        auto project = run / "project";
        fs::create_directory(project);
        core.request("POST", "/api/projects",
                     {{"name", "Documents"},
                      {"root", path_string(project)},
                      {"mode", "backup"},
                      {"backup_interval_minutes", 0}});
        rejects([&] { export_files(data, project / "export"); },
                "Export cannot write inside a project");
        other = data;
        other["platform"] = "macos";
        auto mac = plan(other);
        tinyxml2::XMLDocument document;
        check(document.Parse(mac.at("contents").get<std::string>().c_str()) ==
                  tinyxml2::XML_SUCCESS,
              "LaunchAgent output is valid XML");
        std::vector<std::string> arguments;
        auto dictionary = document.FirstChildElement("plist")->FirstChildElement("dict");
        for (auto key = dictionary->FirstChildElement("key"); key;
             key = key->NextSiblingElement("key"))
            if (std::string(key->GetText()) == "ProgramArguments") {
                auto array = key->NextSiblingElement("array");
                for (auto value = array->FirstChildElement("string"); value;
                     value = value->NextSiblingElement("string"))
                    arguments.emplace_back(value->GetText());
            }
        check(Json(arguments) == mac.at("arguments"),
              "XML parsing recovers Unicode and metacharacter arguments exactly");
        other["plan"] = mac.at("plan");
        check(export_files(other, run / "mac-export").at("receipt").at("platform") == "macos",
              "A macOS service can be prepared on another platform");
        for (const auto &path : std::vector<std::string>{"relative", "/srv/../profile",
                                                         "/srv/bad\npath", "/", "/srv/./profile"}) {
            other = data;
            other["target_state"] = path;
            rejects([&] { plan(other); }, "Unsafe or ambiguous target paths are refused");
        }
        for (const Json port : {Json(80), Json(65536), Json(8484.5), Json("8484")}) {
            other = data;
            other["port"] = port;
            rejects([&] { plan(other); }, "Service ports require an unprivileged integer");
        }
        for (const auto &executable : std::vector<std::string>{
                 "/opt/app'quote/cysynck", "/opt/app?glob/cysynck", "/opt/app\\slash/cysynck"}) {
            other = data;
            other["executable"] = executable;
            rejects([&] { plan(other); },
                    "Executable paths unsupported by systemd are rejected before export");
        }
        rejects(
            [&] {
                cli(core, {"service", "plan", "--platform", "linux", "--executable",
                           "/opt/cysynck/bin/cysynck", "--target-state", "/srv/team", "--port",
                           "8484junk"});
            },
            "CLI ports cannot silently ignore trailing text");
        other = data;
        other["platform"] = "windows";
        rejects([&] { plan(other); }, "Unsupported service platforms are explicit");
        check(cli(core, {"service", "plan", "--platform", "linux", "--executable",
                         data.at("executable"), "--target-state", data.at("target_state")})
                      .at("plan") == linux_plan.at("plan"),
              "CLI and API produce the same reviewed service");
        std::cout << "PASS " << checks << " service checks; fixtures: " << path_string(run) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\nFixtures: " << path_string(run) << '\n';
        return 1;
    }
}
