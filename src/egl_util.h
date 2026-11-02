#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "common.h"

namespace bootanim {

// Same preference order as the device: RGBA8 + depth24 + 4x MSAA first, then plain RGBA8.
inline bool chooseConfig(EGLDisplay dpy, EGLint surfaceType, bool msaa, EGLConfig* out) {
    auto tryChoose = [&](bool depth, bool samples) {
        EGLint a[32]; int n = 0;
        a[n++] = EGL_SURFACE_TYPE;    a[n++] = surfaceType;
        a[n++] = EGL_RENDERABLE_TYPE; a[n++] = EGL_OPENGL_ES3_BIT;
        a[n++] = EGL_RED_SIZE;   a[n++] = 8;
        a[n++] = EGL_GREEN_SIZE; a[n++] = 8;
        a[n++] = EGL_BLUE_SIZE;  a[n++] = 8;
        a[n++] = EGL_ALPHA_SIZE; a[n++] = 8;
        if (depth)   { a[n++] = EGL_DEPTH_SIZE; a[n++] = 24; }
        if (samples) { a[n++] = EGL_SAMPLES; a[n++] = 4; }
        a[n++] = EGL_NONE;
        EGLint cnt = 0;
        return eglChooseConfig(dpy, a, out, 1, &cnt) && cnt > 0;
    };
    if (msaa && tryChoose(true, true)) return true;
    if (msaa) ALOGI("No EGL configs found with depth/samples, trying without");
    return tryChoose(true, false) || tryChoose(false, false);
}

inline EGLContext createES3Context(EGLDisplay dpy, EGLConfig cfg) {
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    return eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, attr);
}

}  // namespace bootanim
