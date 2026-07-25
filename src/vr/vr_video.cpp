/* mupen64plus-VR — Tier-1 OpenXR session layer (packet S9).
 */

#include "vr_video.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>   /* usleep — frame-probe wait-for-READY pacing */

/* OpenGL/Xlib graphics binding for OpenXR. X11 + GLX headers MUST precede
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <X11/Xlib.h>
#include <GL/glx.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#define VR_LOG(...) do { std::fprintf(stderr, "(VR) " __VA_ARGS__); std::fputc('\n', stderr); } while (0)
#define VR_ERR(...) do { std::fprintf(stderr, "(VR) ERROR: " __VA_ARGS__); std::fputc('\n', stderr); } while (0)

namespace {

XrInstance      s_instance      = XR_NULL_HANDLE;
XrSystemId      s_system_id     = XR_NULL_SYSTEM_ID;
XrSession       s_session       = XR_NULL_HANDLE;
XrSpace         s_space         = XR_NULL_HANDLE;   /* LOCAL (seated) reference space */
XrSessionState  s_session_state = XR_SESSION_STATE_UNKNOWN;
std::atomic_bool s_session_running{false};
bool            s_lost              = false;
bool            s_cylinder_supported = false;  /* XR_KHR_composition_layer_cylinder offered */
bool            s_gl_supported       = false;  /* XR_KHR_opengl_enable offered */

/* xrGetOpenGLGraphicsRequirementsKHR is an extension entry point: not exported by
PFN_xrGetOpenGLGraphicsRequirementsKHR s_pfnGetOpenGLReqs = nullptr;

bool CheckXR(XrResult res, const char* what)
{
    if (XR_SUCCEEDED(res))
        return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (s_instance != XR_NULL_HANDLE)
        xrResultToString(s_instance, res, buf);
    VR_ERR("%s failed: %s (%d)", what, buf, static_cast<int>(res));
    return false;
}

const char* SessionStateName(XrSessionState s)
{
    switch (s) {
        case XR_SESSION_STATE_IDLE:         return "IDLE";
        case XR_SESSION_STATE_READY:        return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE:      return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED:      return "FOCUSED";
        case XR_SESSION_STATE_STOPPING:     return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING:      return "EXITING";
        default:                            return "UNKNOWN";
    }
}

void HandleSessionStateChange(XrSessionState new_state)
{
    VR_LOG("Session state: %s -> %s", SessionStateName(s_session_state), SessionStateName(new_state));
    s_session_state = new_state;

    switch (new_state) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin_info = {XR_TYPE_SESSION_BEGIN_INFO};
            begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (CheckXR(xrBeginSession(s_session, &begin_info), "xrBeginSession")) {
                VR_LOG("Session began.");
                s_session_running.store(true, std::memory_order_release);
            } else {
                s_lost = true;
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            s_session_running.store(false, std::memory_order_release);
            CheckXR(xrEndSession(s_session), "xrEndSession");
            VR_LOG("Session ended (runtime request).");
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
        case XR_SESSION_STATE_EXITING:
            s_session_running.store(false, std::memory_order_release);
            s_lost = true;
            break;
        default:
            break;
    }
}

/* Instance + system bring-up. Enables XR_KHR_opengl_enable (+ cylinder if offered),
bool CreateInstanceAndSystem(const char* app_name)
{
    if (s_instance != XR_NULL_HANDLE)
        return true;

    s_cylinder_supported = s_gl_supported = false;
    std::vector<XrExtensionProperties> props;
    {
        uint32_t n = 0;
        if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr)) && n > 0) {
            props.assign(n, {XR_TYPE_EXTENSION_PROPERTIES});
            if (XR_SUCCEEDED(xrEnumerateInstanceExtensionProperties(nullptr, n, &n, props.data()))) {
                for (const auto& p : props) {
                    if (std::strcmp(p.extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0)
                        s_gl_supported = true;
                    if (std::strcmp(p.extensionName, XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME) == 0)
                        s_cylinder_supported = true;
                }
            }
        }
    }

    if (!s_gl_supported) {
        VR_ERR("runtime does not advertise %s — cannot use the GL compositor. Running flat.",
               XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
        return false;   /* S0 fallback territory: a Vulkan-interop path would go here */
    }

    const char* exts[2] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME, nullptr};
    uint32_t ext_count = 1;
    if (s_cylinder_supported)
        exts[ext_count++] = XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME;

    XrInstanceCreateInfo ici = {XR_TYPE_INSTANCE_CREATE_INFO};
    std::strncpy(ici.applicationInfo.applicationName, app_name ? app_name : "mupen64plus-VR",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(ici.applicationInfo.engineName, "mupen64plus-VR", XR_MAX_ENGINE_NAME_SIZE - 1);
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);   /* S0: rig loader maxes at 1.0 */
    ici.enabledExtensionCount = ext_count;
    ici.enabledExtensionNames = exts;

    XrResult res = xrCreateInstance(&ici, &s_instance);
    if (XR_FAILED(res)) {
        VR_LOG("xrCreateInstance failed (%d) — is an OpenXR runtime active? Running flat.", static_cast<int>(res));
        s_instance = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties ip = {XR_TYPE_INSTANCE_PROPERTIES};
    if (CheckXR(xrGetInstanceProperties(s_instance, &ip), "xrGetInstanceProperties"))
        VR_LOG("OpenXR runtime: %s %u.%u.%u", ip.runtimeName,
               XR_VERSION_MAJOR(ip.runtimeVersion), XR_VERSION_MINOR(ip.runtimeVersion),
               XR_VERSION_PATCH(ip.runtimeVersion));
    VR_LOG("Graphics: opengl=%s cylinder-layers=%s (%zu extensions)",
           s_gl_supported ? "yes" : "no", s_cylinder_supported ? "yes" : "no", props.size());

    XrSystemGetInfo sgi = {XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    res = xrGetSystem(s_instance, &sgi, &s_system_id);
    if (XR_FAILED(res)) {
        VR_LOG("No HMD system (%d) — headset connected and awake? Running flat.", static_cast<int>(res));
        xrDestroyInstance(s_instance);
        s_instance = XR_NULL_HANDLE;
        s_system_id = XR_NULL_SYSTEM_ID;
        return false;
    }

    XrSystemProperties sp = {XR_TYPE_SYSTEM_PROPERTIES};
    if (CheckXR(xrGetSystemProperties(s_instance, s_system_id, &sp), "xrGetSystemProperties"))
        VR_LOG("HMD: %s", sp.systemName);

    if (XR_FAILED(xrGetInstanceProcAddr(s_instance, "xrGetOpenGLGraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&s_pfnGetOpenGLReqs))) || s_pfnGetOpenGLReqs == nullptr) {
        VR_ERR("could not load xrGetOpenGLGraphicsRequirementsKHR — running flat.");
        xrDestroyInstance(s_instance);
        s_instance = XR_NULL_HANDLE;
        s_system_id = XR_NULL_SYSTEM_ID;
        return false;
    }

    s_lost = false;
    return true;
}

bool QueryOpenGLGraphicsRequirements()
{
    if (s_pfnGetOpenGLReqs == nullptr)
        return false;
    XrGraphicsRequirementsOpenGLKHR reqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
    if (!CheckXR(s_pfnGetOpenGLReqs(s_instance, s_system_id, &reqs), "xrGetOpenGLGraphicsRequirementsKHR"))
        return false;
    VR_LOG("Runtime supports OpenGL %u.%u - %u.%u",
           XR_VERSION_MAJOR(reqs.minApiVersionSupported), XR_VERSION_MINOR(reqs.minApiVersionSupported),
           XR_VERSION_MAJOR(reqs.maxApiVersionSupported), XR_VERSION_MINOR(reqs.maxApiVersionSupported));
    return true;
}

void DestroySessionInternal()
{
    if (s_session == XR_NULL_HANDLE)
        return;
    if (s_session_running.load(std::memory_order_acquire)) {
        s_session_running.store(false, std::memory_order_release);
        xrEndSession(s_session);
    }
    if (s_space != XR_NULL_HANDLE) {
        xrDestroySpace(s_space);
        s_space = XR_NULL_HANDLE;
    }
    xrDestroySession(s_session);
    s_session = XR_NULL_HANDLE;
    s_session_state = XR_SESSION_STATE_UNKNOWN;
    VR_LOG("XR session destroyed.");
}

void DestroyInstanceInternal()
{
    DestroySessionInternal();
    if (s_instance != XR_NULL_HANDLE) {
        VR_LOG("destroying XR instance...");
        xrDestroyInstance(s_instance);
        s_instance = XR_NULL_HANDLE;
        VR_LOG("XR instance destroyed.");
    }
    s_system_id = XR_NULL_SYSTEM_ID;
    s_pfnGetOpenGLReqs = nullptr;
    s_lost = false;
}

/* Create the XR session bound to the core's existing GLX context. Called from the
__attribute__((unused))
bool CreateSessionGL(Display* dpy, uint32_t visualid, GLXFBConfig fbconfig,
                     GLXDrawable drawable, GLXContext ctx)
{
    if (s_instance == XR_NULL_HANDLE || s_session != XR_NULL_HANDLE)
        return false;

    XrGraphicsBindingOpenGLXlibKHR binding = {XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};
    binding.xDisplay    = dpy;
    binding.visualid    = visualid;
    binding.glxFBConfig = fbconfig;
    binding.glxDrawable = drawable;
    binding.glxContext  = ctx;

    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next     = &binding;
    sci.systemId = s_system_id;
    if (!CheckXR(xrCreateSession(s_instance, &sci, &s_session), "xrCreateSession")) {
        s_session = XR_NULL_HANDLE;
        return false;
    }

    XrReferenceSpaceCreateInfo rsci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation.w = 1.0f;
    if (!CheckXR(xrCreateReferenceSpace(s_session, &rsci, &s_space), "xrCreateReferenceSpace")) {
        DestroySessionInternal();
        return false;
    }

    s_session_state = XR_SESSION_STATE_UNKNOWN;
    VR_LOG("XR session created (OpenGL/GLX).");
    return true;
}

void PumpEvents()
{
    if (s_instance == XR_NULL_HANDLE)
        return;
    for (;;) {
        XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};
        const XrResult res = xrPollEvent(s_instance, &event);
        if (res == XR_EVENT_UNAVAILABLE)
            break;
        if (XR_FAILED(res)) {
            CheckXR(res, "xrPollEvent");
            s_lost = true;
            break;
        }
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                const auto& e = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                if (e.session == s_session)
                    HandleSessionStateChange(e.state);
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                VR_ERR("OpenXR instance loss pending (runtime shutting down).");
                s_session_running.store(false, std::memory_order_release);
                s_lost = true;
                break;
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                VR_LOG("Reference space recentered.");
                break;
            case XR_TYPE_EVENT_DATA_EVENTS_LOST:
                VR_LOG("OpenXR event queue overflowed; some events were lost.");
                break;
            default:
                break;
        }
    }
}

/* ── session/swapchain spike (packet S9→S12 bridge) ──────────────────────────

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB     0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB      0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB  0x00000001
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER      0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_COLOR_BUFFER_BIT
#define GL_COLOR_BUFFER_BIT 0x00004000
#endif

typedef void (*PFN_glGenFramebuffers)(int, unsigned int*);
typedef void (*PFN_glBindFramebuffer)(unsigned int, unsigned int);
typedef void (*PFN_glFramebufferTexture2D)(unsigned int, unsigned int, unsigned int, unsigned int, int);
typedef void (*PFN_glDeleteFramebuffers)(int, const unsigned int*);


typedef GLXContext (*PFN_glXCreateContextAttribsARB)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

/* Create a throwaway GL 3.3-core context on a tiny unmapped X window; fills the five
bool ProbeMakeGLContext(Display** out_dpy, uint32_t* out_visualid, GLXFBConfig* out_fb,
                        Window* out_win, GLXContext* out_ctx)
{
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) { VR_ERR("probe: XOpenDisplay failed (no X display — run in the desktop session)."); return false; }

    int fb_attribs[] = {
        GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, GLX_DOUBLEBUFFER, True, None
    };
    int n = 0;
    GLXFBConfig* fbs = glXChooseFBConfig(dpy, DefaultScreen(dpy), fb_attribs, &n);
    if (fbs == nullptr || n == 0) { VR_ERR("probe: glXChooseFBConfig found no config."); XCloseDisplay(dpy); return false; }
    GLXFBConfig fb = fbs[0];
    XFree(fbs);

    XVisualInfo* vi = glXGetVisualFromFBConfig(dpy, fb);
    if (vi == nullptr) { VR_ERR("probe: glXGetVisualFromFBConfig failed."); XCloseDisplay(dpy); return false; }

    XSetWindowAttributes swa;
    std::memset(&swa, 0, sizeof swa);
    swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
    Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 16, 16, 0,
                               vi->depth, InputOutput, vi->visual, CWColormap, &swa);

    auto glXCreateContextAttribsARB = reinterpret_cast<PFN_glXCreateContextAttribsARB>(
        glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
    GLXContext ctx = nullptr;
    if (glXCreateContextAttribsARB != nullptr) {
        int ctx_attribs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3,
            GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None
        };
        ctx = glXCreateContextAttribsARB(dpy, fb, nullptr, True, ctx_attribs);
    }
    if (ctx == nullptr) { VR_ERR("probe: glXCreateContextAttribsARB (3.3 core) failed."); XDestroyWindow(dpy, win); XFree(vi); XCloseDisplay(dpy); return false; }
    if (!glXMakeCurrent(dpy, win, ctx)) { VR_ERR("probe: glXMakeCurrent failed."); glXDestroyContext(dpy, ctx); XDestroyWindow(dpy, win); XFree(vi); XCloseDisplay(dpy); return false; }

    XFree(vi);
    return true;
}

bool RunSessionProbe()
{
    Display* dpy = nullptr; uint32_t visualid = 0; GLXFBConfig fb = nullptr; Window win = 0; GLXContext ctx = nullptr;
    if (!ProbeMakeGLContext(&dpy, &visualid, &fb, &win, &ctx))
        return false;

    bool ok = false;
    do {
        if (!CreateInstanceAndSystem("m64p-vr-session-diag")) break;
        if (!QueryOpenGLGraphicsRequirements()) break;
        if (!CreateSessionGL(dpy, visualid, fb, win, ctx)) break;

        uint32_t view_count = 0;
        if (!CheckXR(xrEnumerateViewConfigurationViews(s_instance, s_system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr), "xrEnumerateViewConfigurationViews")) break;
        std::vector<XrViewConfigurationView> views(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        if (!CheckXR(xrEnumerateViewConfigurationViews(s_instance, s_system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count, views.data()), "xrEnumerateViewConfigurationViews")) break;
        VR_LOG("Views: %u; eye0 recommended %ux%u", view_count,
               views[0].recommendedImageRectWidth, views[0].recommendedImageRectHeight);

        uint32_t fmt_count = 0;
        if (!CheckXR(xrEnumerateSwapchainFormats(s_session, 0, &fmt_count, nullptr), "xrEnumerateSwapchainFormats")) break;
        std::vector<int64_t> formats(fmt_count);
        if (!CheckXR(xrEnumerateSwapchainFormats(s_session, fmt_count, &fmt_count, formats.data()), "xrEnumerateSwapchainFormats")) break;
        int64_t chosen = formats.empty() ? 0 : formats[0];
        for (int64_t f : formats) { if (f == GL_SRGB8_ALPHA8) { chosen = f; break; } if (f == GL_RGBA8) chosen = f; }
        VR_LOG("Swapchain formats: %u offered; chose 0x%llX (%s)", fmt_count,
               static_cast<unsigned long long>(chosen),
               chosen == GL_SRGB8_ALPHA8 ? "SRGB8_ALPHA8" : chosen == GL_RGBA8 ? "RGBA8" : "runtime-default");

        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format      = chosen;
        sci.sampleCount = 1;
        sci.width       = views[0].recommendedImageRectWidth;
        sci.height      = views[0].recommendedImageRectHeight;
        sci.faceCount   = 1;
        sci.arraySize   = 1;
        sci.mipCount    = 1;
        XrSwapchain swapchain = XR_NULL_HANDLE;
        if (!CheckXR(xrCreateSwapchain(s_session, &sci, &swapchain), "xrCreateSwapchain")) break;

        uint32_t img_count = 0;
        CheckXR(xrEnumerateSwapchainImages(swapchain, 0, &img_count, nullptr), "xrEnumerateSwapchainImages(count)");
        std::vector<XrSwapchainImageOpenGLKHR> images(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        CheckXR(xrEnumerateSwapchainImages(swapchain, img_count, &img_count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())), "xrEnumerateSwapchainImages");
        VR_LOG("Swapchain created: %ux%u, %u GL image(s) (first tex id=%u)",
               sci.width, sci.height, img_count, img_count ? images[0].image : 0u);

        xrDestroySwapchain(swapchain);
        ok = true;
    } while (0);

    DestroyInstanceInternal();
    /* Teardown crashes isolated to WiVRn-26.x/NVIDIA driver faults, AFTER the XR path
    VR_LOG("teardown: destroying GL context..."); if (ctx) glXDestroyContext(dpy, ctx);
    VR_LOG("teardown: destroying X window...");   if (win) XDestroyWindow(dpy, win);
    VR_LOG("teardown: complete (Display intentionally left open — WiVRn X-thread race).");
    return ok;
}

/* Full Tier-1 pipeline spike: session + swapchain + a real frame loop that renders an
bool RunFrameProbe(int max_frames)
{
    Display* dpy = nullptr; uint32_t visualid = 0; GLXFBConfig fb = nullptr; Window win = 0; GLXContext ctx = nullptr;
    if (!ProbeMakeGLContext(&dpy, &visualid, &fb, &win, &ctx))
        return false;

    auto pGenFB = reinterpret_cast<PFN_glGenFramebuffers>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glGenFramebuffers")));
    auto pBindFB = reinterpret_cast<PFN_glBindFramebuffer>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glBindFramebuffer")));
    auto pFBTex = reinterpret_cast<PFN_glFramebufferTexture2D>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glFramebufferTexture2D")));
    auto pDelFB = reinterpret_cast<PFN_glDeleteFramebuffers>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glDeleteFramebuffers")));

    bool ok = false;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    unsigned int fbo = 0;
    do {
        if (pGenFB == nullptr || pBindFB == nullptr || pFBTex == nullptr) { VR_ERR("frame: GL FBO entry points unavailable."); break; }
        if (!CreateInstanceAndSystem("m64p-vr-frame-diag")) break;
        if (!QueryOpenGLGraphicsRequirements()) break;
        if (!CreateSessionGL(dpy, visualid, fb, win, ctx)) break;

        uint32_t view_count = 0;
        if (!CheckXR(xrEnumerateViewConfigurationViews(s_instance, s_system_id,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr), "xrEnumerateViewConfigurationViews")) break;
        std::vector<XrViewConfigurationView> views(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
        xrEnumerateViewConfigurationViews(s_instance, s_system_id, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                          view_count, &view_count, views.data());
        const uint32_t W = views[0].recommendedImageRectWidth, H = views[0].recommendedImageRectHeight;

        uint32_t fmt_count = 0;
        xrEnumerateSwapchainFormats(s_session, 0, &fmt_count, nullptr);
        std::vector<int64_t> formats(fmt_count);
        xrEnumerateSwapchainFormats(s_session, fmt_count, &fmt_count, formats.data());
        int64_t chosen = formats.empty() ? GL_SRGB8_ALPHA8 : formats[0];
        for (int64_t f : formats) { if (f == GL_SRGB8_ALPHA8) { chosen = f; break; } if (f == GL_RGBA8) chosen = f; }

        XrSwapchainCreateInfo sci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = chosen; sci.sampleCount = 1; sci.width = W; sci.height = H;
        sci.faceCount = 1; sci.arraySize = 1; sci.mipCount = 1;
        if (!CheckXR(xrCreateSwapchain(s_session, &sci, &swapchain), "xrCreateSwapchain")) break;

        uint32_t img_count = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &img_count, nullptr);
        std::vector<XrSwapchainImageOpenGLKHR> images(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
        xrEnumerateSwapchainImages(swapchain, img_count, &img_count,
                                   reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

        /* The runtime may have made ITS OWN GL context current during session/swapchain
        VR_LOG("frame: swapchain ready (%ux%u, %u images).", W, H, img_count);
        VR_LOG("frame: current ctx BEFORE rebind = %p (ours=%p)", (void*)glXGetCurrentContext(), (void*)ctx);
        {
            const Bool mc = glXMakeCurrent(dpy, win, ctx);
            VR_LOG("frame: glXMakeCurrent(ours) = %d; current ctx now = %p", (int)mc, (void*)glXGetCurrentContext());
        }
        /* Basic GL sanity (direct libGL calls, no proc-address indirection): is GL alive
        const GLubyte* glver = glGetString(GL_VERSION);
        const GLubyte* glren = glGetString(GL_RENDERER);
        const GLubyte* glven = glGetString(GL_VENDOR);
        VR_LOG("frame: GL_VERSION=%s | RENDERER=%s | VENDOR=%s",
               glver ? reinterpret_cast<const char*>(glver) : "(null)",
               glren ? reinterpret_cast<const char*>(glren) : "(null)",
               glven ? reinterpret_cast<const char*>(glven) : "(null)");
        VR_LOG("frame: creating FBO (glGenFramebuffers=%p)...", reinterpret_cast<void*>(pGenFB));
        pGenFB(1, &fbo);
        VR_LOG("frame: FBO id=%u — entering loop; LOOK IN THE HEADSET.", fbo);

        int rendered = 0, wait_ready = 0;
        while (rendered < max_frames && !s_lost) {
            PumpEvents();
            if (!s_session_running.load(std::memory_order_acquire)) {
                if (++wait_ready > 3000) { VR_ERR("frame: session never reached READY."); break; }
                usleep(2000);
                continue;
            }
            XrFrameWaitInfo wfi = {XR_TYPE_FRAME_WAIT_INFO};
            XrFrameState fs = {XR_TYPE_FRAME_STATE};
            if (!CheckXR(xrWaitFrame(s_session, &wfi, &fs), "xrWaitFrame")) break;
            XrFrameBeginInfo fbi = {XR_TYPE_FRAME_BEGIN_INFO};
            xrBeginFrame(s_session, &fbi);

            XrCompositionLayerQuad quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            std::vector<XrCompositionLayerBaseHeader*> layers;
            if (fs.shouldRender) {
                uint32_t idx = 0;
                XrSwapchainImageAcquireInfo ai = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                xrAcquireSwapchainImage(swapchain, &ai, &idx);
                XrSwapchainImageWaitInfo swi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                swi.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(swapchain, &swi);

                glXMakeCurrent(dpy, win, ctx);   /* runtime may hijack the context in acquire/wait; re-assert ours */
                pBindFB(GL_FRAMEBUFFER, fbo);
                pFBTex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, images[idx].image, 0);
                glViewport(0, 0, static_cast<GLsizei>(W), static_cast<GLsizei>(H));
                const float c = static_cast<float>(rendered % 180) / 180.0f;   /* sweep */
                glClearColor(c, 0.25f, 1.0f - c, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                pBindFB(GL_FRAMEBUFFER, 0);

                XrSwapchainImageReleaseInfo ri = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(swapchain, &ri);

                quad.space = s_space;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = swapchain;
                quad.subImage.imageRect.offset = {0, 0};
                quad.subImage.imageRect.extent = {static_cast<int32_t>(W), static_cast<int32_t>(H)};
                quad.subImage.imageArrayIndex = 0;
                quad.pose.orientation.w = 1.0f;             /* identity; LOCAL-locked   */
                quad.pose.position.z = -1.5f;               /* 1.5 m in front of origin */
                quad.size.width = 1.6f;
                quad.size.height = 1.6f * static_cast<float>(H) / static_cast<float>(W);
                layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
            }
            XrFrameEndInfo fei = {XR_TYPE_FRAME_END_INFO};
            fei.displayTime = fs.predictedDisplayTime;
            fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            fei.layerCount = static_cast<uint32_t>(layers.size());
            fei.layers = layers.data();
            if (!CheckXR(xrEndFrame(s_session, &fei), "xrEndFrame")) break;
            if (++rendered % 90 == 0) VR_LOG("frame loop: %d frames submitted", rendered);
        }
        VR_LOG("frame loop: done, %d frames submitted.", rendered);
        ok = (rendered > 0);
    } while (0);

    if (swapchain != XR_NULL_HANDLE) xrDestroySwapchain(swapchain);
    if (fbo && pDelFB) pDelFB(1, &fbo);
    DestroyInstanceInternal();
    if (ctx) glXDestroyContext(dpy, ctx);   /* see RunSessionProbe teardown notes */
    if (win) XDestroyWindow(dpy, win);
    return ok;
}

} // namespace


extern "C" {

int vr_video_active(void)
{
    return s_session_running.load(std::memory_order_acquire) ? 1 : 0;
}

/* S13 will call this from the VidExt_SetVideoMode tail (context current). For now it
void vr_video_init(int width, int height)
{
    (void)width; (void)height;
    if (!CreateInstanceAndSystem("mupen64plus-VR")) {
        DestroyInstanceInternal();
        return;
    }
    QueryOpenGLGraphicsRequirements();
    VR_LOG("OpenXR bring-up complete; session creation awaits the GL context bind (S13).");
}

uint32_t vr_video_game_fbo(void)
{
    return 0;   /* S12: the stable texture-backed game FBO */
}

void vr_video_present(void)
{
    /* S12: xrWaitFrame/BeginFrame, publish pose, blit game FBO -> swapchain, xrEndFrame.
    if (s_instance != XR_NULL_HANDLE)
        PumpEvents();
}

void vr_video_shutdown(void)
{
    DestroyInstanceInternal();
}

/* Runnable rig probe (the S4-deferred --vr-info). Brings up instance+system+GL
int vr_video_info(void)
{
    VR_LOG("--- mupen64plus-VR OpenXR probe ---");
    if (!CreateInstanceAndSystem("mupen64plus-VR-info")) {
        DestroyInstanceInternal();
        VR_LOG("--- probe: GL OpenXR path NOT ready (see messages above) ---");
        return 1;
    }
    QueryOpenGLGraphicsRequirements();
    VR_LOG("--- probe: GL OpenXR path OK (instance + system + GL requirements) ---");
    DestroyInstanceInternal();
    return 0;
}

int vr_video_session_probe(void)
{
    VR_LOG("--- mupen64plus-VR OpenXR session/swapchain probe ---");
    const bool ok = RunSessionProbe();
    VR_LOG("--- session probe: %s ---", ok ? "OK (session + swapchain created in-headset)"
                                            : "FAILED (see messages above)");
    return ok ? 0 : 1;
}

int vr_video_frame_probe(int max_frames)
{
    VR_LOG("--- mupen64plus-VR OpenXR frame-loop probe ---");
    const bool ok = RunFrameProbe(max_frames > 0 ? max_frames : 600);
    VR_LOG("--- frame probe: %s ---", ok ? "OK (rendered an animated quad in the headset)"
                                          : "FAILED (see messages above)");
    return ok ? 0 : 1;
}

} // extern "C"
