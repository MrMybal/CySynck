#include "desktop_support.hpp"
#include <regex>

namespace cysynck::desktop {
Session::Session(const fs::path &state) : state_(state) {
    fs::create_directories(state_ / "desktop-requests");
    try {
        lease_ = std::make_unique<DiskLock>(state_, "desktop.lock", false);
    } catch (const std::exception &) {
    }
}
void Session::request_show() {
    atomic_write(state_ / "desktop-requests" / (random_id() + ".json"),
                 Json{{"action", "show"}}.dump(), true);
}
bool Session::take_show() {
    if (!primary())
        return false;
    bool show = false;
    int count = 0;
    for (const auto &entry : fs::directory_iterator(state_ / "desktop-requests")) {
        if (++count > 64)
            break;
        if (entry.is_symlink() || !entry.is_regular_file() ||
            !std::regex_match(path_string(entry.path().filename()),
                              std::regex("[a-f0-9]{32}\\.json")))
            continue;
        try {
            auto age = fs::file_time_type::clock::now() - entry.last_write_time();
            if (age < std::chrono::seconds(60))
                show |= Json::parse(read_file(entry.path(), 1024)).value("action", "") == "show";
        } catch (const std::exception &) {
        }
        // The entry was selected from this private directory with a fixed name.
        std::error_code ignored;
        fs::remove(entry.path(), ignored);
    }
    return show;
}
} // namespace cysynck::desktop
