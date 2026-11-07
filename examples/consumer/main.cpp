#include <cysynck/core.hpp>
#include <iostream>

static int run(const std::filesystem::path &state) {
    try {
        cysynck::Core core(state);
        std::cout << core.request("GET", "/api/overview").dump(2) << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    return run(argc > 1 ? std::filesystem::path(argv[1]) : cysynck::default_state());
}
#else
int main(int argc, char **argv) {
    return run(argc > 1 ? std::filesystem::u8path(argv[1]) : cysynck::default_state());
}
#endif
