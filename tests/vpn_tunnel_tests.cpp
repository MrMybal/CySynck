#include "vpn_tunnel.hpp"
#include <ctime>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace cysynck;
namespace {
std::string public_key(char value) { return std::string(42, value) + "A="; }
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void refuses(const std::function<void()> &action, const char *message) {
    bool refused = false;
    try {
        action();
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, message);
}
void environment(const char *key, const std::string &value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    setenv(key, value.c_str(), 1);
#endif
}
int model(int argc, char **argv) {
    auto file = fs::u8path(env("CYSYNCK_VPN_MODEL"));
    auto world = Json::parse(read_file(file));
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.emplace_back(argv[i]);
    world["calls"].push_back(arguments);
    atomic_write(file, world.dump());
    auto command = arguments.at(0);
    if (command == "show" && arguments.at(1) == "interfaces") {
        if (world.value("fail_inventory", false))
            return 1;
        for (auto &[name, key] : world.at("interfaces").items())
            std::cout << name << ' ';
        std::cout << '\n';
        return 0;
    }
    if (command == "show") {
        auto runtime = arguments.at(1);
        if (!world.at("interfaces").contains(runtime))
            return 1;
        if (arguments.at(2) == "public-key")
            std::cout << world.at("interfaces").at(runtime).get<std::string>() << '\n';
        else if (arguments.at(2) == "latest-handshakes")
            std::cout << world.value("handshakes", "");
        else
            return 1;
        return 0;
    }
    bool up = command == "up" || command == "/installtunnelservice";
    bool down = command == "down" || command == "/uninstalltunnelservice";
    if (!up && !down)
        return 1;
    auto config = fs::u8path(arguments.at(1));
    auto logical =
        command == "/uninstalltunnelservice" ? arguments.at(1) : path_string(config.stem());
    auto mappings = fs::u8path(env("CYSYNCK_VPN_MODEL_MAPPINGS"));
    auto runtime = logical == "cysynck" ? "utun43" : "utun42";
    if (up) {
        world["interfaces"][runtime] = world.at("activation_public");
        atomic_write(mappings / (logical + ".name"), runtime);
    } else {
        world["interfaces"].erase(runtime);
        if (world.contains("services"))
            world["services"].erase(logical);
        fs::remove(mappings / (logical + ".name"));
    }
    world["activations"] = world.value("activations", 0) + 1;
    atomic_write(file, world.dump());
    return 0;
}
} // namespace
int main(int argc, char **argv) {
    if (argc > 1) {
        try {
            return model(argc, argv);
        } catch (...) {
            return 21;
        }
    }
    auto root = fs::current_path() / "vpn-model-tests" / random_id();
    try {
        auto rules = vpn_apparmor_rules("/home/test/profil été/vpn/cs-0123456789ab.conf");
        check(rules == "\"/home/test/profil été/vpn/\" r,\n\"/home/test/profil été/vpn/cs-0123456789ab.conf\" r,\n",
              "AppArmor export did not restrict permissions to the exact private tunnel");
        for (auto path : {"relative/cysynck.conf", "/tmp/*/cysynck.conf", "/tmp/@{HOME}/cysynck.conf",
                          "/tmp/../cysynck.conf", "/tmp/[ab]/cysynck.conf", "/tmp/\"bad/cysynck.conf",
                          "/tmp/line\nbreak/cysynck.conf", "/tmp/other.conf"})
            refuses([&] { vpn_apparmor_rules(path); }, "AppArmor path could widen the generated policy");
        fs::create_directories(root / "mappings");
        auto tool = path_string(fs::absolute(fs::u8path(argv[0])));
        auto name = vpn_interface_name("first-isolated-profile");
        check(name.size() <= 15 && name != vpn_interface_name("second-isolated-profile") &&
                  name == vpn_interface_name("first-isolated-profile"),
              "Profile-specific tunnel names are unstable or collide");
        auto mappings = root / "mappings", config = root / (name + ".conf"),
             world_file = root / "world.json";
        environment("CYSYNCK_VPN_MODEL", path_string(world_file));
        environment("CYSYNCK_VPN_MODEL_MAPPINGS", path_string(mappings));
        Json world = {{"interfaces", Json::object()},
                      {"calls", Json::array()},
                      {"activation_public", public_key('A')}};
        auto save = [&] { atomic_write(world_file, world.dump()); };
        auto load = [&] { world = Json::parse(read_file(world_file)); };
        save();
        auto control = [&](const std::string &action) {
            return vpn_tunnel_control(
                action, tool, tool, name, public_key('A'), config,
                "[Interface]\nAddress = 10.77.90.1/32\n", mappings,
                [&](const std::string &logical, const fs::path &, const std::string &) {
                    auto model = Json::parse(read_file(world_file));
                    return model.value("services", Json::object())
                        .value(logical, Json{{"exists", false}, {"owned", false}});
                });
        };
        auto status = [&] {
            return vpn_tunnel_status(tool, name, public_key('A'), config, mappings);
        };
        auto up = control("up");
        load();
        check(up.at("changed") && world.at("activations") == 1,
              "Owned new tunnel was not started through the isolated tool");
        check(vpn_mapped_interface(name, mappings) == "utun42",
              "macOS logical-to-runtime mapping was not resolved");
        auto now = std::time(nullptr);
        world["handshakes"] = public_key('B') + " " + std::to_string(now) + "\n" + public_key('C') +
                              " " + std::to_string(now + 3600) + "\n";
        save();
        auto report = status();
        check(report.at("owned") && report.at("handshakes")[0].at("recent") &&
                  !report.at("handshakes")[1].at("recent"),
              "Future handshake timestamps were reported as recent");
        auto hash = sha256_file(config);
        check(control("up").at("changed") == false && sha256_file(config) == hash,
              "Repeated up overwrote an active tunnel config");
        load();
        world["interfaces"]["utun42"] = public_key('B');
        save();
        refuses([&] { control("up"); }, "Foreign tunnel identity was replaced");
        refuses([&] { control("down"); }, "Foreign tunnel identity was stopped");
        check(sha256_file(config) == hash && status().at("handshakes").empty(),
              "Refused control changed a config or exposed foreign handshakes");
        load();
        world["interfaces"]["utun42"] = public_key('A');
        save();
        check(control("down").at("changed") && !fs::exists(mappings / (name + ".name")),
              "Owned tunnel was not stopped");
        check(control("down").at("already_down"), "An absent owned tunnel was not idempotent");
        load();
        world["services"][name] = {{"exists", true}, {"owned", false}};
        save();
        refuses([&] { control("up"); },
                "A foreign service without a running interface was replaced");
        refuses([&] { control("down"); },
                "A foreign service without a running interface was removed");
        load();
        world["services"][name]["owned"] = true;
        save();
        auto stopped_service = control("up");
        check(stopped_service.at("changed") == false && stopped_service.at("already_up") == false &&
                  stopped_service.at("service_exists") == true,
              "A stopped service was reported as an active tunnel");
        check(control("down").at("changed"), "An owned stopped service could not be removed");
        // The old global name may only be stopped when both its key and old profile config agree.
        load();
        world["interfaces"]["utun43"] = public_key('A');
        save();
        atomic_write(mappings / "cysynck.name", "utun43\n");
        atomic_write(root / "cysynck.conf", "[Interface]\nPostDown = must-never-run\n");
        refuses([&] { control("up"); }, "An owned legacy tunnel was duplicated without migration");
        auto legacy = control("down");
        check(legacy.at("interface") == "cysynck" &&
                  read_file(root / "cysynck.conf").find("PostDown") == std::string::npos,
              "Legacy shutdown reused executable hooks from an old configuration");
        // Another application's global tunnel must survive an independent profile's start/stop.
        load();
        world["interfaces"]["utun43"] = public_key('B');
        save();
        atomic_write(mappings / "cysynck.name", "utun43\n");
        control("up");
        control("down");
        load();
        check(world.at("interfaces").at("utun43") == public_key('B'),
              "Foreign legacy tunnel was changed");
#ifdef _WIN32
        auto command_line =
            L"\"" + fs::u8path(tool).wstring() + L"\" /tunnelservice \"" + config.wstring() + L"\"";
        check(vpn_windows_service_command_owned(command_line, config, tool),
              "Quoted Windows service ownership was not recognized");
        check(!vpn_windows_service_command_owned(command_line, root / "foreign.conf", tool),
              "Foreign Windows service config path was accepted");
        check(!vpn_windows_service_command_owned(command_line + L" extra", config, tool),
              "Unexpected Windows service arguments were accepted");
#endif
        world["fail_inventory"] = true;
        save();
        refuses([&] { control("up"); }, "Failed interface inspection was treated as absence");
        refuses([&] { control("down"); }, "Failed interface inspection allowed deletion");
        atomic_write(mappings / (name + ".name"), "../../foreign");
        refuses([&] { vpn_mapped_interface(name, mappings); },
                "An escaping macOS mapping was accepted");
        atomic_write(mappings / (name + ".name"), "utun42\nextra");
        refuses([&] { vpn_mapped_interface(name, mappings); },
                "A multi-line runtime interface was accepted");
        fs::remove_all(root);
        std::cout << "PASS isolated WireGuard ownership model, profile names, legacy preservation, "
                     "runtime mappings and handshake bounds\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\nOwned fixture preserved: " << path_string(root) << '\n';
        return 1;
    }
}
