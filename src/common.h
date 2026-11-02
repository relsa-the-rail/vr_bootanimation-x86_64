#pragma once
#include <GLES3/gl3.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#define BA_TAG "BootAnimation"
#define ALOGI(...) do { std::fprintf(stderr, "I/" BA_TAG ": "); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ALOGW(...) do { std::fprintf(stderr, "W/" BA_TAG ": "); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define ALOGE(...) do { std::fprintf(stderr, "E/" BA_TAG ": "); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } while (0)

namespace bootanim {

struct WindowSize { uint32_t width = 0, height = 0; };

// BootAnimModel from the original: path + the two config values
struct BootAnimModel {
    std::string path;
    int minSeconds = 0;  // "minimum-animation-time-seconds"
    int minLoops = 0;    // "minimum-animation-loops"
};

// Preview-only knobs (the device reads these from system properties / Strata).
struct Env {
    int screenRotationDeg = 0;          // ro.minui.rotation_degrees (% 180)
    float eyeCenterL = 0.25f;           // Ipd::getEyeCenters fallback (no panel service)
    float eyeCenterR = 0.75f;
    std::string bootanimDir;            // remap /system_ext/etc/bootanim/ -> dir
    std::string systemExtDir;           // remap /system_ext/ -> dir
};

// "Clock": steady_clock on the device. Here it is a virtual clock driven by main()
// so that pause / single-step / deterministic captures work.
struct Clock {
    static double now();                 // virtual seconds (what the animators see)
    static void set(double t);
    static double real();                // monotonic wall clock, seconds
};

std::string mapPath(const Env& env, const std::string& devicePath);
GLuint createProgram(const char* vs, const char* fs);   // 0 on failure
bool checkGlError(const char* op);                      // true if an error was pending

// Animator == the original's abstract base (vtable: init, draw, saveScreen,
// windDown, checkExit, getType, checkTransition, setUpdateProgress)
class Animator {
public:
    virtual ~Animator() = default;
    virtual bool init() = 0;
    virtual void draw() = 0;
    virtual void saveScreen() {}
    virtual void windDown() {}
    virtual bool checkExit() { return true; }
    virtual int getType() { return 0; }
    virtual bool checkTransition() { return false; }
    virtual void setWindowSize(WindowSize) {}
};

}  // namespace bootanim
