// vr_bootanimation preview -- x86-64 / Wayland re-implementation of Meta Quest's
// /system/bin/vr_bootanimation, rebuilt from its AArch64 code.  See README.md.
#include "common.h"
#include "logo_animator.h"
#include "platform.h"
#include "renderer.h"
#include "json.hpp"
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <getopt.h>
#include <sys/stat.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace bootanim;

namespace {

struct Config {
    bool demo = false;
    std::string configPath;
    std::vector<BootAnimModel> models;
    int dumpFrames = 0;       // -f
    int splashFrame = -1;     // -s
};

struct Opts {
    Config cfg;
    Env env;
    PlatformConfig plat;
    std::string backend = "auto";       // auto | wayland | headless
    std::string traceDir = "traces";
    double fps = 30.0;                  // the device loop is capped at ~30 fps (33.3 ms)
    std::vector<double> atTimes;        // --at t1,t2,...
    std::string shotPath;               // --shot out.png
    int frames = 0;                     // --frames N (deterministic offline render)
    double exitAfter = -1;              // --exit-after S  (simulates service.bootanim.exit)
};

void usage() {
    std::fputs(
"Usage: vr_bootanimation [-d] [-m <model>] [-c <config>] [-f <n>] [-s <n>] [-h]\n"
"  -c: use config file to load models\n"
"  -d: demo mode - will not exit automatically\n"
"  -m: use custom gltf file to load meta model\n"
"  -f: dump number of frames to file, up to a maximum of 10 frames\n"
"  -s: dump splash bitmaps from frame n (generally n=0)\n"
"  -h: print help\n"
"With neither -c nor -m the meta.bin / meta-colors.png logo is used (like the device).\n"
"\nPreview options:\n"
"  --bootanim-dir DIR   map /system_ext/etc/bootanim/* to DIR/*   (e.g. /mnt/system_ext_a/etc/bootanim)\n"
"  --system-ext DIR     map /system_ext/* to DIR/*                  (e.g. /mnt/system_ext_a)\n"
"  --size WxH           window / framebuffer size, both eyes (default 1832x960 = Quest 2 panel / 2)\n"
"  --fps N              frame cap (default 30, like the device; 0 = uncapped)\n"
"  --rotation DEG       ro.minui.rotation_degrees (default 0)\n"
"  --eye-centers L,R    eye centre x as fraction of width (default 0.25,0.75)\n"
"  --no-msaa            disable 4x MSAA (the device asks for it)\n"
"  --fullscreen         start fullscreen\n"
"  --wayland | --headless   force backend (auto: Wayland if WAYLAND_DISPLAY is set)\n"
"  --at T[,T...] --shot FILE.png   render stills at animation time(s) T (seconds since start)\n"
"  --frames N           render N deterministic frames at --fps into --trace-dir (headless)\n"
"  --trace-dir DIR      where -f/-s/--frames/S-key write PNGs (default ./traces)\n"
"  --exit-after S       behave as if service.bootanim.exit was set after S seconds\n"
"Keys (window): Esc/Q quit, Space pause, Left/Right step (paused), R restart, F fullscreen,\n"
"               S screenshot, E simulate exit signal\n", stderr);
}

bool parseSize(const char* s, uint32_t& w, uint32_t& h) {
    unsigned a = 0, b = 0;
    if (std::sscanf(s, "%ux%u", &a, &b) != 2 || !a || !b) return false;
    w = a; h = b; return true;
}

// loadConfigFile(): {"bootanim": [ {path, minimum-animation-loops, minimum-animation-time-seconds}, ... ]}
bool loadConfigFile(const std::string& devPath, const Env& env, Config& cfg) {
    std::string path = mapPath(env, devPath);
    std::ifstream f(path);
    if (!f) { ALOGE("Failed to open config file: %s", path.c_str()); return false; }
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded()) { ALOGE("Failed to parse config file: %s", path.c_str()); return false; }
    if (!j.contains("bootanim")) { ALOGE("Config file does not contain bootanim object"); return false; }
    const auto& b = j["bootanim"];
    auto addModel = [&](const nlohmann::json& m) {
        BootAnimModel bm;
        if (m.is_object() && m.contains("path") && m["path"].is_string()) bm.path = m["path"].get<std::string>();
        else if (m.is_string()) bm.path = m.get<std::string>();
        else return;
        if (m.is_object()) {
            bm.minLoops = m.value("minimum-animation-loops", 0);
            bm.minSeconds = m.value("minimum-animation-time-seconds", 0);
        }
        cfg.models.push_back(bm);
    };
    if (b.is_array()) for (const auto& m : b) addModel(m);
    else if (b.is_object()) { if (b.contains("path")) addModel(b); else for (auto it = b.begin(); it != b.end(); ++it) addModel(it.value()); }
    if (cfg.models.empty()) { ALOGE("No models found in config file"); return false; }
    if (cfg.models.size() > 2) { ALOGE("Boot animation can only support 2 models"); return false; }
    return true;
}

std::unique_ptr<Animator> makeAnimator(const Config& cfg, int idx, WindowSize sz, const Env& env) {
    if (!cfg.models.empty()) return std::make_unique<Renderer>(cfg.models[idx], sz, env);
    return std::make_unique<LogoAnimator>(sz, env);
}

bool readPixels(WindowSize sz, GLuint src, std::vector<unsigned char>& rgba) {
    // The default framebuffer is multisampled (4x MSAA like the device) and ES 3 forbids
    // glReadPixels on it, so resolve through a blit into a single-sample FBO first.
    GLuint fbo = 0, rb = 0;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, (GLsizei)sz.width, (GLsizei)sz.height);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glFinish();
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBlitFramebuffer(0, 0, sz.width, sz.height, 0, 0, sz.width, sz.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    rgba.assign((size_t)sz.width * sz.height * 4, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, sz.width, sz.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    bool bad = checkGlError("readPixels");
    glBindFramebuffer(GL_FRAMEBUFFER, src);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rb);
    return !bad;
}

void flipRows(std::vector<unsigned char>& px, int w, int h) {
    std::vector<unsigned char> row((size_t)w * 4);
    for (int y = 0; y < h / 2; ++y) {
        unsigned char* a = &px[(size_t)y * w * 4];
        unsigned char* b = &px[(size_t)(h - 1 - y) * w * 4];
        std::memcpy(row.data(), a, row.size()); std::memcpy(a, b, row.size()); std::memcpy(b, row.data(), row.size());
    }
}

bool savePng(const std::string& path, WindowSize sz, GLuint src) {
    std::vector<unsigned char> px;
    if (!readPixels(sz, src, px)) return false;
    flipRows(px, (int)sz.width, (int)sz.height);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;   // opaque, like a screenshot of the display
    return stbi_write_png(path.c_str(), (int)sz.width, (int)sz.height, 4, px.data(), (int)sz.width * 4) != 0;
}

// -s: left/right eye crops (the device additionally trims empty rows/columns; we keep the full half)
bool saveEyes(const std::string& dir, int /*frame*/, WindowSize sz, GLuint src) {
    std::vector<unsigned char> px;
    if (!readPixels(sz, src, px)) return false;
    flipRows(px, (int)sz.width, (int)sz.height);
    int eyeW = (int)(sz.width / 2);
    for (int eye = 0; eye < 2; ++eye) {
        std::vector<unsigned char> crop((size_t)eyeW * sz.height * 4);
        for (uint32_t y = 0; y < sz.height; ++y)
            std::memcpy(&crop[(size_t)y * eyeW * 4], &px[((size_t)y * sz.width + eye * eyeW) * 4], (size_t)eyeW * 4);
        char name[256];
        std::snprintf(name, sizeof name, "%s/meta-logo-%s.bmp", dir.c_str(), eye ? "right" : "left");
        stbi_write_bmp(name, eyeW, (int)sz.height, 4, crop.data());
        ALOGI("saved %s", name);
    }
    return true;
}

void mkdirs(const std::string& d) { ::mkdir(d.c_str(), 0755); }

int runStills(Opts& o, Platform& plat) {
    WindowSize sz = plat.size();
    Clock::set(0.0);
    auto anim = makeAnimator(o.cfg, 0, sz, o.env);
    if (!anim->init()) { ALOGE("Loading animation failed, see log for details"); return 1; }
    int n = 0;
    for (double t : o.atTimes) {
        Clock::set(t);
        glBindFramebuffer(GL_FRAMEBUFFER, plat.drawTarget());
        anim->draw();
        std::string path = o.shotPath;
        if (o.atTimes.size() > 1) {
            size_t dot = path.rfind('.');
            char suf[64]; std::snprintf(suf, sizeof suf, "_t%06.3f", t);
            path = dot == std::string::npos ? path + suf : path.substr(0, dot) + suf + path.substr(dot);
        }
        if (!savePng(path, sz, plat.drawTarget())) { ALOGE("Failed to write %s", path.c_str()); return 1; }
        ALOGI("t=%.3fs -> %s", t, path.c_str());
        ++n;
    }
    return n ? 0 : 1;
}

int runFrames(Opts& o, Platform& plat) {
    WindowSize sz = plat.size();
    mkdirs(o.traceDir);
    Clock::set(0.0);
    auto anim = makeAnimator(o.cfg, 0, sz, o.env);
    if (!anim->init()) { ALOGE("Loading animation failed, see log for details"); return 1; }
    for (int i = 0; i < o.frames; ++i) {
        Clock::set(i / o.fps);
        glBindFramebuffer(GL_FRAMEBUFFER, plat.drawTarget());
        anim->draw();
        char name[512]; std::snprintf(name, sizeof name, "%s/frame_%04d.png", o.traceDir.c_str(), i);
        if (!savePng(name, sz, plat.drawTarget())) return 1;
    }
    ALOGI("wrote %d frames to %s", o.frames, o.traceDir.c_str());
    return 0;
}

int runWindow(Opts& o, Platform& plat) {
    WindowSize sz = plat.size();
    double paused_at = -1, pausedTotal = 0, t0 = Clock::real();
    auto vnow = [&]() { return (paused_at >= 0 ? paused_at : Clock::real()) - pausedTotal - t0; };
    Clock::set(0.0);
    int modelIdx = 0;
    auto anim = makeAnimator(o.cfg, modelIdx, sz, o.env);
    if (!anim->init()) { ALOGE("Loading animation failed, see log for details"); return 1; }
    ALOGI("Preview running on %s, %ux%u (eye %ux%u). Esc/Q quits.", plat.name(), sz.width, sz.height, sz.width / 2, sz.height);

    mkdirs(o.traceDir);
    const double period = o.fps > 0 ? 1.0 / o.fps : 0.0;
    double deadline = Clock::real();
    int frame = 0, shot = 0;
    bool wantShot = false, savedMode = false, exitRequested = false;
    double exitSignalAt = -1;
    bool running = true;

    while (running) {
        Clock::set(vnow());
        sz = plat.size();
        anim->setWindowSize(sz);
        glBindFramebuffer(GL_FRAMEBUFFER, plat.drawTarget());
        anim->draw();

        char name[512];
        if (frame < o.cfg.dumpFrames) {
            std::snprintf(name, sizeof name, "%s/frame_%03d.png", o.traceDir.c_str(), frame);
            if (savePng(name, sz, plat.drawTarget())) ALOGI("dumpFrame: Finished saving frame %d to %s", frame, name);
        }
        if (o.cfg.splashFrame >= 0 && frame == o.cfg.splashFrame) saveEyes(o.traceDir, frame, sz, plat.drawTarget());
        if (wantShot) {
            std::snprintf(name, sizeof name, "%s/shot_%03d.png", o.traceDir.c_str(), shot++);
            if (savePng(name, sz, plat.drawTarget())) ALOGI("screenshot: %s", name);
            wantShot = false;
        }
        plat.swapBuffers();
        ++frame;

        // wait for the next frame deadline while serving window events
        deadline += period;
        double now = Clock::real();
        if (deadline < now - 0.25) deadline = now;
        for (;;) {
            double rem = deadline - Clock::real();
            int ms = rem > 0 ? (int)std::ceil(rem * 1000.0) : 0;
            if (paused_at >= 0 && ms == 0) ms = 16;
            if (!plat.pump(ms)) { running = false; break; }
            Key k;
            while (plat.takeKey(k)) {
                switch (k) {
                    case Key::Quit: running = false; break;
                    case Key::Pause:
                        if (paused_at < 0) paused_at = Clock::real();
                        else { pausedTotal += Clock::real() - paused_at; paused_at = -1; }
                        ALOGI(paused_at >= 0 ? "paused at t=%.3f" : "resumed (t=%.3f)", vnow());
                        break;
                    case Key::StepBack: case Key::StepFwd:
                        if (paused_at >= 0) {
                            double step = (k == Key::StepFwd ? 1.0 : -1.0) / (o.fps > 0 ? o.fps : 30.0);
                            pausedTotal -= step;   // moves virtual time by +/- one frame
                            ALOGI("t=%.3f", vnow());
                            deadline = Clock::real();
                        }
                        break;
                    case Key::Restart: {
                        Clock::set(vnow());
                        anim = makeAnimator(o.cfg, modelIdx, plat.size(), o.env);
                        if (!anim->init()) { ALOGE("re-init failed"); running = false; }
                        savedMode = false; exitRequested = false; exitSignalAt = -1;
                        break; }
                    case Key::Fullscreen: plat.toggleFullscreen(); break;
                    case Key::Screenshot: wantShot = true; break;
                    case Key::SimulateExit: if (exitSignalAt < 0) { exitSignalAt = vnow(); ALOGI("service.bootanim.exit simulated"); } break;
                }
            }
            if (!running || Clock::real() >= deadline) break;
        }
        if (!running) break;

        // ---- BootAnimation::checkExit / checkTransition ----
        double v = vnow();
        if (!o.cfg.demo) {
            if (!savedMode && v >= 61.0) {
                ALOGW("boot animation running too long: %ds, enable screen saving mode", (int)v);
                anim->saveScreen(); savedMode = true;
            }
            if (o.exitAfter >= 0 && exitSignalAt < 0 && v >= o.exitAfter) { exitSignalAt = v; ALOGI("service.bootanim.exit set"); }
            if (exitSignalAt >= 0 && !exitRequested) { anim->windDown(); exitRequested = true; }
            if (exitRequested && anim->checkExit()) { ALOGI("Exiting Boot Animation"); break; }
        } else if (exitSignalAt >= 0) {
            ALOGI("(demo mode: exit signal ignored)"); exitSignalAt = -1;
        }
        if (anim->getType() != 0 && modelIdx + 1 < (int)o.cfg.models.size() && anim->checkTransition()) {
            anim->windDown();
            ++modelIdx;
            Clock::set(vnow());
            anim = makeAnimator(o.cfg, modelIdx, plat.size(), o.env);
            if (!anim->init()) { ALOGE("Loading animation failed, see log for details"); return 1; }
            ALOGI("Transitioned to model %d", modelIdx);
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Opts o;
    enum { OPT_BDIR = 1000, OPT_SYSEXT, OPT_SIZE, OPT_FPS, OPT_ROT, OPT_EYES, OPT_NOMSAA, OPT_FS,
           OPT_WL, OPT_HL, OPT_AT, OPT_SHOT, OPT_FRAMES, OPT_TRACE, OPT_EXIT };
    static const option longopts[] = {
        {"bootanim-dir", 1, nullptr, OPT_BDIR}, {"system-ext", 1, nullptr, OPT_SYSEXT}, {"size", 1, nullptr, OPT_SIZE},
        {"fps", 1, nullptr, OPT_FPS}, {"rotation", 1, nullptr, OPT_ROT}, {"eye-centers", 1, nullptr, OPT_EYES},
        {"no-msaa", 0, nullptr, OPT_NOMSAA}, {"fullscreen", 0, nullptr, OPT_FS}, {"wayland", 0, nullptr, OPT_WL},
        {"headless", 0, nullptr, OPT_HL}, {"at", 1, nullptr, OPT_AT}, {"shot", 1, nullptr, OPT_SHOT},
        {"frames", 1, nullptr, OPT_FRAMES}, {"trace-dir", 1, nullptr, OPT_TRACE}, {"exit-after", 1, nullptr, OPT_EXIT},
        {"help", 0, nullptr, 'h'}, {nullptr, 0, nullptr, 0}};

    std::vector<BootAnimModel> cliModels;
    int c;
    while ((c = getopt_long(argc, argv, "dm:hf:s:c:", longopts, nullptr)) != -1) {
        switch (c) {
            case 'd': o.cfg.demo = true; break;
            case 'm': { BootAnimModel m; m.path = optarg; cliModels.push_back(m); break; }
            case 'c': o.cfg.configPath = optarg; break;
            case 'f': o.cfg.dumpFrames = std::atoi(optarg); break;
            case 's': o.cfg.splashFrame = std::atoi(optarg); break;
            case 'h': usage(); return 0;
            case OPT_BDIR: o.env.bootanimDir = optarg; break;
            case OPT_SYSEXT: o.env.systemExtDir = optarg; break;
            case OPT_SIZE: if (!parseSize(optarg, o.plat.width, o.plat.height)) { ALOGE("bad --size"); return 2; } break;
            case OPT_FPS: o.fps = std::atof(optarg); break;
            case OPT_ROT: o.env.screenRotationDeg = std::atoi(optarg) % 180; break;
            case OPT_EYES: if (std::sscanf(optarg, "%f,%f", &o.env.eyeCenterL, &o.env.eyeCenterR) != 2) { ALOGE("bad --eye-centers"); return 2; } break;
            case OPT_NOMSAA: o.plat.msaa = false; break;
            case OPT_FS: o.plat.fullscreen = true; break;
            case OPT_WL: o.backend = "wayland"; break;
            case OPT_HL: o.backend = "headless"; break;
            case OPT_AT: { std::stringstream ss(optarg); std::string tok; while (std::getline(ss, tok, ',')) o.atTimes.push_back(std::atof(tok.c_str())); break; }
            case OPT_SHOT: o.shotPath = optarg; break;
            case OPT_FRAMES: o.frames = std::atoi(optarg); break;
            case OPT_TRACE: o.traceDir = optarg; break;
            case OPT_EXIT: o.exitAfter = std::atof(optarg); break;
            default: usage(); return 2;
        }
    }

    if (!o.cfg.configPath.empty() && !cliModels.empty()) { ALOGE("Cannot specify both config and metamodel options"); return 2; }
    if (!o.cfg.configPath.empty()) { if (!loadConfigFile(o.cfg.configPath, o.env, o.cfg)) return 1; }
    else o.cfg.models = cliModels;
    if (o.cfg.dumpFrames < 0) { ALOGW("Invalid number of frames to dump: %d", o.cfg.dumpFrames); o.cfg.dumpFrames = 0; }
    if (o.cfg.dumpFrames > 10) { ALOGW("Maximum number of frames to dump is 10"); o.cfg.dumpFrames = 10; }
    if ((o.cfg.dumpFrames || o.cfg.splashFrame >= 0) && !o.cfg.demo) {
        ALOGW("Dumping frames is only supported in demo mode"); o.cfg.dumpFrames = 0; o.cfg.splashFrame = -1;
    }
    if (o.cfg.demo) ALOGI("Running in demo mode");

    const bool offline = !o.atTimes.empty() || o.frames > 0;
    if (!o.atTimes.empty() && o.shotPath.empty()) { ALOGE("--at needs --shot FILE.png"); return 2; }
    bool wantHeadless = o.backend == "headless" || (o.backend == "auto" && offline);
    if (o.backend == "auto" && !offline && !std::getenv("WAYLAND_DISPLAY")) {
        ALOGE("WAYLAND_DISPLAY is not set. Run inside a Wayland session, or use --headless with --at/--frames.");
        return 1;
    }
    auto plat = wantHeadless ? createHeadlessPlatform() : createWaylandPlatform();
    o.plat.title = "vr_bootanimation preview";
    if (!plat->init(o.plat)) { ALOGE("Platform '%s' failed to initialise", plat->name()); return 1; }
    {
        const char* r = (const char*)glGetString(GL_RENDERER);
        const char* v = (const char*)glGetString(GL_VERSION);
        GLint smp = 0, sb = 0; glGetIntegerv(GL_SAMPLES, &smp); glGetIntegerv(GL_SAMPLE_BUFFERS, &sb);
        ALOGI("GL_RENDERER=%s | %s | samples=%d buffers=%d", r ? r : "?", v ? v : "?", smp, sb);
    }
    int rc;
    if (!o.atTimes.empty()) rc = runStills(o, *plat);
    else if (o.frames > 0) rc = runFrames(o, *plat);
    else rc = runWindow(o, *plat);
    return rc;
}
