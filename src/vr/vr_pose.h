/* mupen64plus-VR — head-pose store (packet S1/S5).
 */
#ifndef M64P_VR_POSE_H
#define M64P_VR_POSE_H

#include "api/m64p_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* XR pump only. Replace the stored pose; vr_pose owns the monotonic frame stamp
void vr_pose_publish(const m64p_vr_pose* p);

void vr_pose_invalidate(void);

int vr_pose_get(m64p_vr_pose* out);

/* Exported for the video plugin (listed in api_export.ver). Same as vr_pose_get;
EXPORT int CALL CoreVR_GetHeadPose(m64p_vr_pose* out);

/* Temporal-stereo eye handshake: the video plugin reports which eye (0=left,
EXPORT void CALL CoreVR_SetStereoEye(int eye);
int vr_stereo_eye_get(void);

/* Dual-render swap-mode channel: 1 = the next SwapBuffers is stage-only (upload the
EXPORT void CALL CoreVR_SetSwapMode(int mode);
int vr_swap_mode_get(void);

/* Resolved per-game stereo depth for the video plugin (listed in api_export.ver): fills
EXPORT void CALL CoreVR_GetStereo(float* separation, float* convergence);

/* Flatten an orientation quaternion (x,y,z,w) to its yaw about world +Y: the heading of
int vr_pose_flatten_yaw(float qx, float qy, float qz, float qw, float* yaw_out);

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_POSE_H */
