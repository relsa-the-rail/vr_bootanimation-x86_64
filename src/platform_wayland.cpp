// Native Wayland backend: wl_compositor + xdg-shell (+ xdg-decoration if available) + wayland-egl.
// Keyboard input uses raw evdev scancodes (layout independent), so no xkbcommon is needed.
#include "platform.h"
#include "egl_util.h"
#include <wayland-client.h>
#include <wayland-egl.h>
#include "xdg-shell-client-protocol.h"
#ifdef HAVE_XDG_DECORATION
#include "xdg-decoration-unstable-v1-client-protocol.h"
#endif
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <deque>

namespace bootanim {

class WaylandPlatform : public Platform {
public:
    ~WaylandPlatform() override { shutdown(); }

    bool init(const PlatformConfig& cfg) override {
        cfg_ = cfg;
        w_ = cfg.width; h_ = cfg.height;
        dpy_ = wl_display_connect(nullptr);
        if (!dpy_) { ALOGE("Cannot connect to a Wayland compositor (is WAYLAND_DISPLAY set?)"); return false; }
        reg_ = wl_display_get_registry(dpy_);
        static const wl_registry_listener rl = {&WaylandPlatform::onGlobal, &WaylandPlatform::onGlobalRemove};
        wl_registry_add_listener(reg_, &rl, this);
        wl_display_roundtrip(dpy_);
        if (!comp_ || !wm_) { ALOGE("Compositor lacks wl_compositor or xdg_wm_base"); return false; }

        static const xdg_wm_base_listener wml = {[](void*, xdg_wm_base* b, uint32_t s) { xdg_wm_base_pong(b, s); }};
        xdg_wm_base_add_listener(wm_, &wml, this);

        surf_ = wl_compositor_create_surface(comp_);
        xsurf_ = xdg_wm_base_get_xdg_surface(wm_, surf_);
        static const xdg_surface_listener xsl = {&WaylandPlatform::onXdgSurfaceConfigure};
        xdg_surface_add_listener(xsurf_, &xsl, this);
        top_ = xdg_surface_get_toplevel(xsurf_);
        static const xdg_toplevel_listener tl = {&WaylandPlatform::onTopConfigure, &WaylandPlatform::onTopClose,
                                                  nullptr, nullptr};
        xdg_toplevel_add_listener(top_, &tl, this);
        xdg_toplevel_set_title(top_, cfg.title.c_str());
        xdg_toplevel_set_app_id(top_, "vr_bootanimation_preview");
#ifdef HAVE_XDG_DECORATION
        if (decoMgr_) {
            deco_ = zxdg_decoration_manager_v1_get_toplevel_decoration(decoMgr_, top_);
            zxdg_toplevel_decoration_v1_set_mode(deco_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
#endif
        if (cfg.fullscreen) { xdg_toplevel_set_fullscreen(top_, nullptr); fullscreen_ = true; }
        wl_surface_commit(surf_);
        while (!configured_) if (wl_display_dispatch(dpy_) < 0) return false;

        win_ = wl_egl_window_create(surf_, (int)w_, (int)h_);
        if (!win_) { ALOGE("wl_egl_window_create failed"); return false; }

        auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (getPlatformDisplay) edpy_ = getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, dpy_, nullptr);
        if (edpy_ == EGL_NO_DISPLAY) edpy_ = eglGetDisplay((EGLNativeDisplayType)dpy_);
        if (edpy_ == EGL_NO_DISPLAY || !eglInitialize(edpy_, nullptr, nullptr)) { ALOGE("eglInitialize failed"); return false; }
        EGLConfig c;
        if (!chooseConfig(edpy_, EGL_WINDOW_BIT, cfg.msaa, &c)) { ALOGE("Failed to find any suitable EGL config"); return false; }
        esurf_ = eglCreateWindowSurface(edpy_, c, (EGLNativeWindowType)win_, nullptr);
        ectx_ = createES3Context(edpy_, c);
        if (esurf_ == EGL_NO_SURFACE || ectx_ == EGL_NO_CONTEXT) { ALOGE("EGL surface/context creation failed (0x%x)", eglGetError()); return false; }
        if (!eglMakeCurrent(edpy_, esurf_, esurf_, ectx_)) { ALOGE("eglMakeCurrent failed"); return false; }
        eglSwapInterval(edpy_, 0);   // pacing is done by the main loop (30 fps like the device)
        return true;
    }

    void swapBuffers() override { eglSwapBuffers(edpy_, esurf_); }

    bool pump(int timeoutMs) override {
        wl_display_dispatch_pending(dpy_);
        while (wl_display_prepare_read(dpy_) != 0) wl_display_dispatch_pending(dpy_);
        wl_display_flush(dpy_);
        pollfd pfd{wl_display_get_fd(dpy_), POLLIN, 0};
        int r = poll(&pfd, 1, timeoutMs < 0 ? -1 : timeoutMs);
        if (r > 0) wl_display_read_events(dpy_); else wl_display_cancel_read(dpy_);
        wl_display_dispatch_pending(dpy_);
        if (pendingW_ && pendingH_ && (pendingW_ != w_ || pendingH_ != h_)) {
            w_ = pendingW_; h_ = pendingH_;
            if (win_) wl_egl_window_resize(win_, (int)w_, (int)h_, 0, 0);
        }
        return !closed_ && wl_display_get_error(dpy_) == 0;
    }

    WindowSize size() const override { return {w_, h_}; }
    bool takeKey(Key& k) override { if (keys_.empty()) return false; k = keys_.front(); keys_.pop_front(); return true; }
    void toggleFullscreen() override {
        fullscreen_ = !fullscreen_;
        if (fullscreen_) xdg_toplevel_set_fullscreen(top_, nullptr); else xdg_toplevel_unset_fullscreen(top_);
    }
    const char* name() const override { return "wayland"; }

private:
    void shutdown() {
        if (edpy_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(edpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (esurf_ != EGL_NO_SURFACE) eglDestroySurface(edpy_, esurf_);
            if (ectx_ != EGL_NO_CONTEXT) eglDestroyContext(edpy_, ectx_);
            eglTerminate(edpy_);
        }
        if (win_) wl_egl_window_destroy(win_);
#ifdef HAVE_XDG_DECORATION
        if (deco_) zxdg_toplevel_decoration_v1_destroy(deco_);
#endif
        if (top_) xdg_toplevel_destroy(top_);
        if (xsurf_) xdg_surface_destroy(xsurf_);
        if (surf_) wl_surface_destroy(surf_);
        if (kb_) wl_keyboard_destroy(kb_);
        if (dpy_) wl_display_disconnect(dpy_);
        dpy_ = nullptr;
    }

    // --- registry
    static void onGlobal(void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t ver) {
        auto* s = (WaylandPlatform*)d;
        if (!std::strcmp(iface, wl_compositor_interface.name))
            s->comp_ = (wl_compositor*)wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
        else if (!std::strcmp(iface, xdg_wm_base_interface.name))
            s->wm_ = (xdg_wm_base*)wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        else if (!std::strcmp(iface, wl_seat_interface.name)) {
            s->seat_ = (wl_seat*)wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
            static const wl_seat_listener sl = {&WaylandPlatform::onSeatCaps, [](void*, wl_seat*, const char*) {}};
            wl_seat_add_listener(s->seat_, &sl, s);
        }
#ifdef HAVE_XDG_DECORATION
        else if (!std::strcmp(iface, zxdg_decoration_manager_v1_interface.name))
            s->decoMgr_ = (zxdg_decoration_manager_v1*)wl_registry_bind(r, name, &zxdg_decoration_manager_v1_interface, 1);
#endif
    }
    static void onGlobalRemove(void*, wl_registry*, uint32_t) {}

    // --- xdg
    static void onXdgSurfaceConfigure(void* d, xdg_surface* xs, uint32_t serial) {
        auto* s = (WaylandPlatform*)d;
        xdg_surface_ack_configure(xs, serial);
        s->configured_ = true;
    }
    static void onTopConfigure(void* d, xdg_toplevel*, int32_t w, int32_t h, wl_array*) {
        auto* s = (WaylandPlatform*)d;
        if (w > 0 && h > 0) { s->pendingW_ = (uint32_t)w; s->pendingH_ = (uint32_t)h; }
    }
    static void onTopClose(void* d, xdg_toplevel*) { ((WaylandPlatform*)d)->closed_ = true; }

    // --- input (evdev codes)
    static void onSeatCaps(void* d, wl_seat* seat, uint32_t caps) {
        auto* s = (WaylandPlatform*)d;
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->kb_) {
            s->kb_ = wl_seat_get_keyboard(seat);
            static const wl_keyboard_listener kl = {
                [](void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t) { close(fd); },
                [](void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {},
                [](void*, wl_keyboard*, uint32_t, wl_surface*) {},
                &WaylandPlatform::onKey,
                [](void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {},
                [](void*, wl_keyboard*, int32_t, int32_t) {}};
            wl_keyboard_add_listener(s->kb_, &kl, s);
        }
    }
    static void onKey(void* d, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
        if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
        auto* s = (WaylandPlatform*)d;
        switch (key) {
            case 1: case 16: s->keys_.push_back(Key::Quit); break;        // Esc, Q
            case 57: s->keys_.push_back(Key::Pause); break;               // Space
            case 19: s->keys_.push_back(Key::Restart); break;             // R
            case 33: case 87: s->keys_.push_back(Key::Fullscreen); break; // F, F11
            case 31: s->keys_.push_back(Key::Screenshot); break;          // S
            case 105: s->keys_.push_back(Key::StepBack); break;           // Left
            case 106: s->keys_.push_back(Key::StepFwd); break;            // Right
            case 18: s->keys_.push_back(Key::SimulateExit); break;        // E
        }
    }

    PlatformConfig cfg_;
    wl_display* dpy_ = nullptr; wl_registry* reg_ = nullptr; wl_compositor* comp_ = nullptr;
    xdg_wm_base* wm_ = nullptr; wl_seat* seat_ = nullptr; wl_keyboard* kb_ = nullptr;
    wl_surface* surf_ = nullptr; xdg_surface* xsurf_ = nullptr; xdg_toplevel* top_ = nullptr;
    wl_egl_window* win_ = nullptr;
#ifdef HAVE_XDG_DECORATION
    zxdg_decoration_manager_v1* decoMgr_ = nullptr; zxdg_toplevel_decoration_v1* deco_ = nullptr;
#endif
    EGLDisplay edpy_ = EGL_NO_DISPLAY; EGLSurface esurf_ = EGL_NO_SURFACE; EGLContext ectx_ = EGL_NO_CONTEXT;
    uint32_t w_ = 0, h_ = 0, pendingW_ = 0, pendingH_ = 0;
    bool configured_ = false, closed_ = false, fullscreen_ = false;
    std::deque<Key> keys_;
};

std::unique_ptr<Platform> createWaylandPlatform() { return std::make_unique<WaylandPlatform>(); }

}  // namespace bootanim
