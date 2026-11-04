#include <cstdlib>
#include <cstring>
#include <cysynck/c_api.h>
#include <cysynck/core.hpp>

static void output(char **out, const std::string &s) {
    if (!out)
        return;
    *out = static_cast<char *>(std::malloc(s.size() + 1));
    if (*out)
        std::memcpy(*out, s.c_str(), s.size() + 1);
}
extern "C" {
cysynck_handle cysynck_open(const char *path, char **error) {
    if (error)
        *error = nullptr;
    try {
        return new cysynck::Core(path && *path ? std::filesystem::u8path(path)
                                               : cysynck::default_state());
    } catch (const std::exception &e) {
        output(error, cysynck::Json{{"error", e.what()}}.dump(
                          -1, ' ', false, cysynck::Json::error_handler_t::replace));
        return nullptr;
    }
}
int cysynck_request(cysynck_handle handle, const char *method, const char *path, const char *body,
                    char **result) {
    if (result)
        *result = nullptr;
    try {
        if (!handle || !method || !path || !result)
            throw std::runtime_error("Invalid API argument");
        auto value = static_cast<cysynck::Core *>(handle)->request(
            method, path, body && *body ? cysynck::Json::parse(body) : cysynck::Json::object());
        output(result, value.dump());
        return *result ? 0 : 2;
    } catch (const std::exception &e) {
        output(result, cysynck::Json{{"error", e.what()}}.dump(
                           -1, ' ', false, cysynck::Json::error_handler_t::replace));
        return 1;
    }
}
void cysynck_free(char *value) { std::free(value); }
void cysynck_close(cysynck_handle handle) { delete static_cast<cysynck::Core *>(handle); }
unsigned int cysynck_abi_version() { return 1; }
}
