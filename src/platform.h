#pragma once
#include "common.h"
#include <memory>

namespace bootanim {

enum class Key { Quit, Pause, Restart, Fullscreen, Screenshot, StepBack, StepFwd, SimulateExit };

struct PlatformConfig {
    uint32_t width = 1832, height = 960;
    bool msaa = true;
    bool fullscreen = false;
    std::string title = "vr_bootanimation preview";
};

class Platform {
public:
    virtual ~Platform() = default;
    virtual bool init(const PlatformConfig& cfg) = 0;   // window + EGL, context current
    virtual void swapBuffers() = 0;
    // Process events, waiting at most timeoutMs. Returns false once the user asked to close.
    virtual bool pump(int timeoutMs) = 0;
    virtual WindowSize size() const = 0;
    virtual bool takeKey(Key& k) = 0;                   // pops one pending key press
    virtual void toggleFullscreen() {}
    virtual const char* name() const = 0;
    virtual bool interactive() const { return true; }
    // Framebuffer the animators draw into / that screenshots read from (0 = the window surface).
    virtual GLuint drawTarget() const { return 0; }
};

std::unique_ptr<Platform> createWaylandPlatform();
std::unique_ptr<Platform> createHeadlessPlatform();

}  // namespace bootanim
