#include <cysynck/core.hpp>
#include <iostream>

int main(int argc, char **argv) {
    try {
        cysynck::Core core(argc > 1 ? std::filesystem::u8path(argv[1]) : cysynck::default_state());
        std::cout << core.request("GET", "/api/overview").dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
