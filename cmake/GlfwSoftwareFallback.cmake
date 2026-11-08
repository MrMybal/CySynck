# GLFW 3.4 requests an accelerated NSGL pixel format unconditionally. GPU-less
# macOS VMs still provide Apple's legacy software renderer. Prefer hardware,
# then let NSGL choose software only when the first pixel format is unavailable.
# Upstream discussion: https://github.com/glfw/glfw/issues/2080
if(NOT DEFINED GLFW_SOURCE_DIR)
  message(FATAL_ERROR "Pass GLFW_SOURCE_DIR for the pinned GLFW dependency")
endif()
set(context "${GLFW_SOURCE_DIR}/src/nsgl_context.m")
file(READ "${context}" source)
set(marker "// CySynck: permit the Apple software renderer after hardware fails.")
string(FIND "${source}" "${marker}" patched)
if(NOT patched EQUAL -1)
  return()
endif()
set(original [=[    window->context.nsgl.pixelFormat =
        [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];
    if (window->context.nsgl.pixelFormat == nil)
]=])
set(replacement [=[    window->context.nsgl.pixelFormat =
        [[NSOpenGLPixelFormat alloc] initWithAttributes:attribs];
    // CySynck: permit the Apple software renderer after hardware fails.
    if (window->context.nsgl.pixelFormat == nil)
    {
        assert(attribs[0] == NSOpenGLPFAAccelerated);
        window->context.nsgl.pixelFormat =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:(attribs + 1)];
    }
    if (window->context.nsgl.pixelFormat == nil)
]=])
string(FIND "${source}" "${original}" position)
if(position EQUAL -1)
  message(FATAL_ERROR "Pinned GLFW NSGL implementation changed; review the software-renderer patch")
endif()
string(REPLACE "${original}" "${replacement}" source "${source}")
file(WRITE "${context}" "${source}")
