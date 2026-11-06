#include "engine_options.hpp"
#include <iostream>
using namespace cysynck;
int main() {
    try {
        for (const auto &help : {"  --no-port-probing  Disable probing\n  --log-file=PATH\n",
                                 "[--no-port-probing] [--log-file=PATH]"}) {
            if (syncthing_option(help, {"--no-port-probing", "--skip-port-probing"}) !=
                    "--no-port-probing" ||
                syncthing_option(help, {"--log-file", "--logfile"}) != "--log-file")
                return 1;
        }
        if (syncthing_option("  --skip-port-probing\r\n  --logfile=PATH\r\n",
                             {"--no-port-probing", "--skip-port-probing"}) !=
                "--skip-port-probing" ||
            syncthing_option("  --logfile=PATH\n", {"--log-file", "--logfile"}) != "--logfile")
            return 1;
        for (const auto &help : {"", "--log-file-name=PATH", "prefix--log-file=PATH"}) {
            bool rejected = false;
            try {
                syncthing_option(help, {"--log-file", "--logfile"});
            } catch (const std::exception &) {
                rejected = true;
            }
            if (!rejected)
                return 1;
        }
        std::cout << "PASS modern/legacy Syncthing options and exact flag boundaries\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
