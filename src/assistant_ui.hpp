#pragma once
#include <memory>
#include <cysynck/core.hpp>
namespace cysynck::assistant {
class DesktopBridge;
class Panel {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Panel(Core &, std::shared_ptr<DesktopBridge> = {});
    ~Panel();
    void draw(bool french);
    void changed(const Json &events);
    int listen(int port, bool allow_write = false);
};
} // namespace cysynck::assistant
