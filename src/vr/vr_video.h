/* mupen64plus-VR — Tier-1 XR session + compositor (packet S1 stub; S9/S12/S13 fill).
 */
#ifndef M64P_VR_VIDEO_H
#define M64P_VR_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int vr_video_active(void);

/* From the VidExt_SetVideoMode tail: create the stable "game FBO" and bring up
void vr_video_init(int width, int height);

/* The stable texture-backed FBO GLideN64 composites into (returned from
uint32_t vr_video_game_fbo(void);

/* From VidExt_GL_SwapBuffers: publish head pose, blit game FBO -> XR swapchain,
void vr_video_present(void);

void vr_video_shutdown(void);

/* Runnable OpenXR probe (the --vr-info / MUPEN_VR_INFO path): bring up instance +
int vr_video_info(void);

/* Session/swapchain spike (packet S9->S12 bridge): make a throwaway GL 3.3 context,
int vr_video_session_probe(void);

/* Full Tier-1 pipeline spike (packet S9->S12): session + swapchain + a real frame loop
int vr_video_frame_probe(int max_frames);

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_VIDEO_H */
