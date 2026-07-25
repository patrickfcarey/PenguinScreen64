/* mupen64plus-VR — Tier-1 OpenXR compositor, Vulkan path (packet S12). extern "C"
#ifndef M64P_VR_VIDEO_VK_H
#define M64P_VR_VIDEO_VK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runnable Vulkan OpenXR probe: bring up the XR instance + system, create a VkInstance
int vr_video_vk_info(void);

/* Vulkan Tier-1 pipeline spike: session + swapchain + a frame loop that clears the
int vr_video_vk_frame_probe(int max_frames);


/* Bring up the compositor for a width×height game frame (XR session + Vulkan device +
int vr_video_vk_init(int width, int height);

/* Present one game frame: `rgba_pixels` is a width*height*4 RGBA8 buffer in GL
/* eye: 0 = left, 1 = right (temporal stereo — the core alternates per frame in lockstep
int vr_video_vk_submit(const void* rgba_pixels, int width, int height, int eye);

/* Dual-render path (both eyes each game frame): stage() uploads one eye's pixels
int vr_video_vk_stage(const void* rgba_pixels, int width, int height, int eye);
int vr_video_vk_present(void);

/* Request a screen recenter (any thread; e.g. the VR Recenter hotkey). Consumed on the
void vr_video_vk_recenter(void);
int  vr_video_vk_toggle_follow(void);

void vr_video_vk_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_VIDEO_VK_H */
