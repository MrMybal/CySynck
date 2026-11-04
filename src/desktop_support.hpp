#pragma once
#include <cysynck/core.hpp>
#include <array>
#include <deque>
#include <memory>
#include <string>

namespace cysynck::desktop {
enum class TrayAction { show = 1, open_web, backup_all, quit };
struct TrayLabels {
    std::string show, open_web, backup_all, quit, tooltip;
    unsigned char red = 255, green = 149, blue = 0;
    bool operator==(const TrayLabels &) const = default;
};
class Tray {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Tray(const TrayLabels &labels);
    ~Tray();
    bool available() const;
    std::string reason() const;
    void update(const TrayLabels &labels);
    void pump();
    bool next(TrayAction &action);
};
// One desktop per state directory. Only 'show' requests are accepted; engine
// ownership and the server remain independent of this presentation lock.
class Session {
    fs::path state_;
    std::unique_ptr<DiskLock> lease_;

  public:
    explicit Session(const fs::path &state);
    bool primary() const { return bool(lease_); }
    void request_show();
    bool take_show();
};
inline bool close_hides(bool preference, bool tray_available) {
    return preference && tray_available;
}
} // namespace cysynck::desktop
