#include <cysynck/core.hpp>
// GCC's legacy predefined macro must not collide with a backend enum identifier.
#ifndef linux
#define linux 1
#endif
#include "startup.hpp"
static_assert(cysynck::desktop::StartupBackend::freedesktop !=
              cysynck::desktop::StartupBackend::windows);
