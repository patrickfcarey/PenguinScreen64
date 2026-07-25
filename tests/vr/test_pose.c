#include "vr_tests.h"
#include "vr/vr_pose.h"

#include <math.h>

#define VR_TP_PI 3.14159265358979323846f

static int yaw_near(float a, float b) { return fabsf(a - b) < 1e-3f; }

int test_pose(void)
{
    int fails = 0;
    m64p_vr_pose p;

    vr_pose_invalidate();
    VR_CHECK(&fails, vr_pose_get(&p) == 0, "default get returns invalid");
    VR_CHECK(&fails, p.valid == 0, "default pose valid==0");

    m64p_vr_pose in;
    in.orient_x = 0.1f; in.orient_y = 0.2f; in.orient_z = 0.3f; in.orient_w = 0.9f;
    in.pos_x = 1.0f; in.pos_y = 2.0f; in.pos_z = 3.0f; in.valid = 1; in.frame = 999;
    vr_pose_publish(&in);
    VR_CHECK(&fails, vr_pose_get(&p) == 1, "get after publish returns valid");
    VR_CHECK(&fails, p.orient_x == 0.1f && p.pos_z == 3.0f, "published values round-trip");
    VR_CHECK(&fails, p.frame != 999, "publish owns the frame counter (caller frame ignored)");
    uint32_t f1 = p.frame;
    vr_pose_publish(&in);
    vr_pose_get(&p);
    VR_CHECK(&fails, p.frame > f1, "frame is monotonic across publishes");

    vr_pose_invalidate();
    VR_CHECK(&fails, vr_pose_get(&p) == 0 && p.valid == 0, "invalidate => invalid");
    VR_CHECK(&fails, p.orient_w == 1.0f, "invalidate => identity quaternion");

    VR_CHECK(&fails, CoreVR_GetHeadPose(0) == 0, "CoreVR_GetHeadPose(NULL) == 0");
    vr_pose_publish(&in);
    VR_CHECK(&fails, CoreVR_GetHeadPose(&p) == 1 && p.valid == 1, "CoreVR_GetHeadPose returns validity");

    {
        float yaw = 99.0f;
        float s, c, sp, cp;

        VR_CHECK(&fails, vr_pose_flatten_yaw(0, 0, 0, 1, &yaw) == 1 && yaw_near(yaw, 0.0f),
                 "flatten: identity => yaw 0");

        s = sinf(VR_TP_PI / 4.0f); c = cosf(VR_TP_PI / 4.0f);
        VR_CHECK(&fails, vr_pose_flatten_yaw(0, s, 0, c, &yaw) == 1 && yaw_near(yaw, VR_TP_PI / 2.0f),
                 "flatten: pure yaw +90 => +pi/2");

        VR_CHECK(&fails, vr_pose_flatten_yaw(0, 1, 0, 0, &yaw) == 1 && yaw_near(fabsf(yaw), VR_TP_PI),
                 "flatten: yaw 180 => |pi|");

        sp = sinf(VR_TP_PI / 8.0f); cp = cosf(VR_TP_PI / 8.0f);
        VR_CHECK(&fails, vr_pose_flatten_yaw(sp, 0, 0, cp, &yaw) == 1 && yaw_near(yaw, 0.0f),
                 "flatten: pure pitch => yaw 0");

        /* composed yaw(+90) * pitch(45): the pitch must drop out, yaw survives.
        {
            float sy = s, cy = c;                     /* yaw 90: half-angle 45 */
            float qx = cy * sp, qy = cp * sy, qz = -sy * sp, qw = cy * cp;
            VR_CHECK(&fails, vr_pose_flatten_yaw(qx, qy, qz, qw, &yaw) == 1 && yaw_near(yaw, VR_TP_PI / 2.0f),
                     "flatten: yaw90*pitch45 => +pi/2 (pitch dropped)");
        }

        sp = sinf(-VR_TP_PI / 4.0f); cp = cosf(-VR_TP_PI / 4.0f);
        yaw = 123.0f;
        VR_CHECK(&fails, vr_pose_flatten_yaw(sp, 0, 0, cp, &yaw) == 0 && yaw == 123.0f,
                 "flatten: straight down => degenerate (0), yaw untouched");
    }

    printf("%d check(s) failed\n", fails);
    return fails;
}
