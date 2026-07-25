/* mupen64plus-VR — head-pose store implementation (packet S1 baseline; S5 owns
#include "vr_pose.h"
#include <math.h>
#include <string.h>

static m64p_vr_pose s_pose;      /* zero-init => {valid=0, frame=0} */
static uint32_t     s_frame = 0; /* publisher-owned monotonic counter */

void vr_pose_publish(const m64p_vr_pose* p)
{
    m64p_vr_pose t = *p;
    t.frame = ++s_frame;         /* monotonic by construction */
    s_pose = t;
}

void vr_pose_invalidate(void)
{
    m64p_vr_pose t;
    memset(&t, 0, sizeof t);
    t.orient_w = 1.0f;           /* identity quaternion */
    t.valid = 0;
    t.frame = ++s_frame;
    s_pose = t;
}

int vr_pose_get(m64p_vr_pose* out)
{
    return s_pose.valid;
}

EXPORT int CALL CoreVR_GetHeadPose(m64p_vr_pose* out)
{
    if (out == NULL)
        return 0;
    return s_pose.valid;
}

/* ── temporal-stereo eye handshake ────────────────────────────────────────────
static int s_stereo_eye = -1;

EXPORT void CALL CoreVR_SetStereoEye(int eye)
{
    s_stereo_eye = (eye != 0) ? 1 : 0;
}

int vr_stereo_eye_get(void)
{
    return s_stereo_eye;
}

/* Swap-mode channel (dual-render): the plugin marks its mid-frame LEFT-eye swap as
static int s_swap_mode = 0;   /* 0 = stage+present (normal), 1 = stage-only */

EXPORT void CALL CoreVR_SetSwapMode(int mode)
{
    s_swap_mode = (mode != 0) ? 1 : 0;
}

int vr_swap_mode_get(void)
{
    return s_swap_mode;
}

int vr_pose_flatten_yaw(float qx, float qy, float qz, float qw, float* yaw_out)
{
    /* forward = quat-rotate({0,0,-1}); only its XZ components are needed:
    float fx = -2.0f * (qy * qw + qz * qx);
    float fz = -1.0f + 2.0f * (qx * qx + qy * qy);

    /* Straight up/down: the XZ projection vanishes and heading is undefined —
    if (fx * fx + fz * fz < 1.0e-4f)
        return 0;

    return 1;
}
