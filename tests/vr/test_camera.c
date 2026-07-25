/* mupen64plus-VR — camera-engine unit tests (packet S10). Owns this file.
#include "vr_tests.h"
#include "vr/vr_camera.h"
#include <math.h>
#include <string.h>

static uint32_t bits_of(float f) { uint32_t b; memcpy(&b, &f, sizeof b); return b; }

static void reset_profile(vr_profile* p)
{
    memset(p, 0, sizeof *p);
    p->valid = 1;
    p->headlook_mode = VR_HL_MEMWRITE;
    p->cam_base = 0;
}

static void one_op(vr_profile* p, uint32_t off, uint8_t axis, uint8_t enc,
                   float scale, float bias, float lo, float hi)
{
    p->n_ops = 1;
    p->ops[0].off = off;
    p->ops[0].axis = axis;
    p->ops[0].encoding = enc;
    p->ops[0].scale = scale;
    p->ops[0].bias = bias;
    p->ops[0].clamp_lo = lo;
    p->ops[0].clamp_hi = hi;
}

static void yaw_pose(m64p_vr_pose* pose, float theta)
{
    memset(pose, 0, sizeof *pose);
    pose->orient_y = sinf(theta * 0.5f);
    pose->orient_w = cosf(theta * 0.5f);
    pose->valid = 1;
}

int test_camera(void)
{
    int fails = 0;
    int n;
    float got;
    uint32_t dram[4096];             /* 16 KiB mock RDRAM */
    vr_profile prof;
    vr_camera_state st;
    m64p_vr_pose pose;
    const uint32_t SZ = (uint32_t)sizeof dram;

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    one_op(&prof, 0x10, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 1.0f, -1e30f, 1e30f);
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 1, "one op => one write");
    VR_CHECK(&fails, dram[0x10 / 4] == 0x3F800000u, "1.0f encodes to IEEE-754 0x3F800000");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    one_op(&prof, 0x20, VR_AXIS_YAW, VR_ENC_FLOAT32, 1.0f, 0.0f, -10.0f, 10.0f);
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    memcpy(&got, &dram[0x20 / 4], sizeof got);
    VR_CHECK(&fails, n == 1, "yaw op writes once");
    VR_CHECK(&fails, fabsf(got - 0.5f) < 1e-4f, "yaw 0.5rad quat round-trips to ~0.5");

    reset_profile(&prof);
    one_op(&prof, 0x00, VR_AXIS_YAW, VR_ENC_FLOAT32, 1000.0f, 0.0f, -0.25f, 0.25f);
    memset(dram, 0, sizeof dram);
    yaw_pose(&pose, 0.5f);          /* +500 rad => clamps to clamp_hi */
    (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    memcpy(&got, &dram[0], sizeof got);
    VR_CHECK(&fails, got == 0.25f, "positive overshoot clamps to clamp_hi");
    memset(dram, 0, sizeof dram);
    yaw_pose(&pose, -0.5f);         /* -500 rad => clamps to clamp_lo */
    (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    memcpy(&got, &dram[0], sizeof got);
    VR_CHECK(&fails, got == -0.25f, "negative overshoot clamps to clamp_lo");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    one_op(&prof, 0x10, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 1.0f, -1e30f, 1e30f);
    yaw_pose(&pose, 0.5f);
    pose.valid = 0;
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 0 && dram[0x10 / 4] == 0u, "pose.valid=0 => zero writes");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    prof.headlook_mode = VR_HL_MATRIX;
    one_op(&prof, 0x10, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 1.0f, -1e30f, 1e30f);
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 0, "headlook_mode != memwrite => zero writes");

    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, NULL, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 0, "NULL profile => zero writes");

    reset_profile(&prof);
    one_op(&prof, 0x40, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 1.0f, -1e30f, 1e30f);
    prof.n_guards = 1;
    prof.guards[0].addr = 0x80000100u;         /* KSEG0 => masks to physical 0x100 */
    prof.guards[0].equals = 0xABCD1234u;
    yaw_pose(&pose, 0.5f);
    memset(dram, 0, sizeof dram);
    dram[0x100 / 4] = 0xABCD1234u;             /* guard satisfied */
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 1 && dram[0x40 / 4] == 0x3F800000u, "guard match => armed, writes");
    memset(dram, 0, sizeof dram);
    dram[0x100 / 4] = 0x00000000u;             /* guard broken */
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 0 && dram[0x40 / 4] == 0u, "guard mismatch => disarmed, zero writes");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    prof.cam_base = 0x80000000u;
    one_op(&prof, 0x08, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 2.0f, -1e30f, 1e30f);
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 1 && dram[0x08 / 4] == bits_of(2.0f), "KSEG0 base masks to physical word");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    one_op(&prof, SZ, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.0f, 1.0f, -1e30f, 1e30f);
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 0, "offset at end of RDRAM => no write");

    memset(dram, 0, sizeof dram);
    reset_profile(&prof);
    prof.n_ops = 1;
    prof.ops[0].off = 0x02;                    /* low halfword of word 0 */
    prof.ops[0].axis = VR_AXIS_YAW;
    prof.ops[0].encoding = VR_ENC_S16_RAD;
    prof.ops[0].scale = 0.0f;
    prof.ops[0].bias = 1.0f;                   /* 1.0 rad => Q3.12 4096 = 0x1000 */
    prof.ops[0].clamp_lo = -8.0f;
    prof.ops[0].clamp_hi = 8.0f;
    dram[0] = 0xAAAA5555u;                      /* high halfword is the sibling */
    yaw_pose(&pose, 0.5f);
    n = (vr_camera_state_reset(&st), vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st));
    VR_CHECK(&fails, n == 1, "s16 op writes once");
    VR_CHECK(&fails, (dram[0] & 0x0000FFFFu) == 0x1000u, "S16_RAD 1.0 => Q3.12 0x1000 in low halfword");
    VR_CHECK(&fails, (dram[0] & 0xFFFF0000u) == 0xAAAA0000u, "s16 write preserves sibling halfword");

    {
        /* mock layout: pointer at 0x100 -> block at 0x800; yaw float at block+0x48;
        float gv; uint32_t bits;
        memset(dram, 0, SZ);
        memset(&prof, 0, sizeof prof);
        memset(&pose, 0, sizeof pose);
        prof.valid = 1;
        prof.headlook_mode = VR_HL_MEMWRITE;
        prof.cam_baseptr = 0x80000100u;
        dram[0x100 >> 2] = 0x80000800u;             /* player block pointer     */
        prof.n_guards = 2;
        prof.guards[0].addr = 0x80000200u; prof.guards[0].equals = 4; prof.guards[0].rel = 0;
        prof.guards[1].addr = 0x168;       prof.guards[1].equals = 0; prof.guards[1].rel = 1;
        dram[0x200 >> 2] = 4;                       /* abs guard passes          */
        prof.n_ops = 1;
        prof.ops[0].axis = VR_AXIS_YAW;
        prof.ops[0].off = 0x48;
        prof.ops[0].scale = 1.0f;
        prof.ops[0].encoding = VR_ENC_FLOAT32;
        prof.ops[0].unit = VR_UNIT_DEG;
        prof.ops[0].mode = VR_MODE_COMPOSE;
        prof.ops[0].wrap = 360;
        prof.ops[0].clamp_lo = -1.0e30f; prof.ops[0].clamp_hi = 1.0e30f;
        gv = 350.0f;                                 /* game yaw at arm           */
        memcpy(&bits, &gv, 4); dram[(0x800 + 0x48) >> 2] = bits;

        vr_camera_state_reset(&st);
        yaw_pose(&pose, 0.0f);                       /* arm at head yaw 0         */
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        VR_CHECK(&fails, n == 1 && st.armed == 1, "GE-shape: arms and writes");
        memcpy(&gv, &dram[(0x800 + 0x48) >> 2], 4);
        VR_CHECK(&fails, gv > 349.9f && gv < 350.1f, "compose at arm == captured game yaw");

        yaw_pose(&pose, 20.0f * 0.017453292519943295f);  /* head +20 deg          */
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st); /* NO reset: stays armed */
        memcpy(&gv, &dram[(0x800 + 0x48) >> 2], 4);
        VR_CHECK(&fails, n == 1 && gv > 9.9f && gv < 10.1f, "350 + 20 wraps to 10 deg");

        dram[(0x800 + 0x168) >> 2] = 1;
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        VR_CHECK(&fails, n == 0 && st.armed == 0, "rel guard mismatch disarms");

        dram[(0x800 + 0x168) >> 2] = 0;
        dram[0x100 >> 2] = 0x12345678u;
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        VR_CHECK(&fails, n == 0, "bogus baseptr value blocks writes");
    }

    /* 8) ANCHORED mode: live baseline follows the game's own turning; the head
    {
        float head0 = 0.4f;                    /* head yaw at arm (rad) */
        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x20, VR_AXIS_YAW, VR_ENC_FLOAT32, 1.0f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_ANCHORED;
        prof.ops[0].wrap = 360;
        prof.ops[0].unit = VR_UNIT_DEG;

        got = 350.0f;                          /* game camera sits at 350 deg */
        memcpy(&dram[0x20 >> 2], &got, 4);
        yaw_pose(&pose, head0);
        vr_camera_state_reset(&st);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 350.0f) < 1e-3f,
                 "anchored arm VI writes the game value back (no snap)");

        got = 4.0f;
        memcpy(&dram[0x20 >> 2], &got, 4);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 4.0f) < 1e-3f,
                 "game's own turn folds into the baseline across the seam (head still)");

        yaw_pose(&pose, head0 + 0.174533f);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 14.0f) < 1e-2f,
                 "head offset rides on top of the live baseline");

        yaw_pose(&pose, head0);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 4.0f) < 1e-2f,
                 "head back at reference == game baseline (zero accumulated drift)");

        dram[0x20 >> 2] = 0x7FC00000u;         /* NaN */
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        VR_CHECK(&fails, n == 0, "anchored skips the write when the game value is NaN");
    }

    /* 9) DELTA mode (pcsx2-VR doc 07 §2 / MouseInjector feel): per-VI head delta
    {
        float head0 = 0.2f;
        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x30, VR_AXIS_YAW, VR_ENC_FLOAT32, 1.0f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_DELTA;
        prof.ops[0].wrap = 360;
        prof.ops[0].unit = VR_UNIT_DEG;

        got = 100.0f;                          /* game camera at 100 deg */
        memcpy(&dram[0x30 >> 2], &got, 4);
        yaw_pose(&pose, head0);
        vr_camera_state_reset(&st);
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);   /* arm: record sample */
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 100.0f) < 1e-3f,
                 "delta arm VI leaves the game value untouched");

        yaw_pose(&pose, head0 + 0.174533f);
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 110.0f) < 1e-2f, "head delta adds to the live value");

        /* the GAME (stick) moves the value to 200 between VIs; head still ->
        got = 200.0f;
        memcpy(&dram[0x30 >> 2], &got, 4);
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 200.0f) < 1e-2f,
                 "game's own writes persist exactly (no attribution, no rubber-band)");

        yaw_pose(&pose, head0);
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 190.0f) < 1e-2f, "delta is relative, mouse-style");
    }

    /* 10) sensitivity multiplies the HEAD term only: sens=1.0 reproduces the old
    {
        float head0 = 0.2f;

        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x30, VR_AXIS_YAW, VR_ENC_FLOAT32, 1.0f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_DELTA;
        prof.ops[0].wrap = 360;
        prof.ops[0].unit = VR_UNIT_DEG;
        got = 100.0f;
        memcpy(&dram[0x30 >> 2], &got, 4);
        yaw_pose(&pose, head0);
        vr_camera_state_reset(&st);
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);   /* arm */
        yaw_pose(&pose, head0 + 0.174533f);                  /* head +10 deg */
        vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 110.0f) < 1e-2f, "sens=1.0 preserves the +10 delta");

        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x30, VR_AXIS_YAW, VR_ENC_FLOAT32, 1.0f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_DELTA;
        prof.ops[0].wrap = 360;
        prof.ops[0].unit = VR_UNIT_DEG;
        got = 100.0f;
        memcpy(&dram[0x30 >> 2], &got, 4);
        yaw_pose(&pose, head0);
        vr_camera_state_reset(&st);
        vr_camera_eval(dram, SZ, &prof, &pose, 2.0f, &st);   /* arm */
        yaw_pose(&pose, head0 + 0.174533f);                  /* head +10 deg */
        vr_camera_eval(dram, SZ, &prof, &pose, 2.0f, &st);
        memcpy(&got, &dram[0x30 >> 2], 4);
        VR_CHECK(&fails, fabsf(got - 120.0f) < 1e-2f, "sens=2.0 doubles the injected delta");
    }

    /* 11) compose seam: |scale| != 1 means the output 360-wrap can NOT self-heal a
    {
        const float d2r = 0.017453292519943295f;
        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x20, VR_AXIS_YAW, VR_ENC_FLOAT32, 0.5f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_COMPOSE;
        prof.ops[0].unit = VR_UNIT_DEG;
        prof.ops[0].wrap = 360;

        got = 100.0f;                          /* game yaw at arm */
        memcpy(&dram[0x20 >> 2], &got, 4);
        yaw_pose(&pose, 179.0f * d2r);         /* arm with head at +179 deg */
        vr_camera_state_reset(&st);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 100.0f) < 1e-2f,
                 "compose arm VI writes the captured game value (no snap)");

        yaw_pose(&pose, -179.0f * d2r);        /* head crosses the seam to -179 deg */
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        memcpy(&got, &dram[0x20 >> 2], 4);
        VR_CHECK(&fails, n == 1 && fabsf(got - 101.0f) < 0.1f,
                 "compose shortest-arc: +2deg head arc * 0.5 => +1deg, not ~-179");
    }

    /* 12) encoding guard: live-value read-back (DELTA/ANCHORED) is float32-only. A
    {
        float head0 = 0.2f;
        memset(dram, 0, sizeof dram);
        reset_profile(&prof);
        one_op(&prof, 0x30, VR_AXIS_YAW, VR_ENC_S16_RAD, 1.0f, 0.0f, -1e30f, 1e30f);
        prof.ops[0].mode = VR_MODE_DELTA;
        prof.ops[0].wrap = 360;
        prof.ops[0].unit = VR_UNIT_DEG;
        dram[0x30 >> 2] = 0x11112222u;         /* sentinel: must stay untouched */
        yaw_pose(&pose, head0);
        vr_camera_state_reset(&st);
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);   /* arm */
        yaw_pose(&pose, head0 + 0.174533f);                      /* head +10 deg */
        n = vr_camera_eval(dram, SZ, &prof, &pose, 1.0f, &st);
        VR_CHECK(&fails, n == 0 && dram[0x30 >> 2] == 0x11112222u,
                 "s16 delta op is skipped (float32-only read-back), no write");
    }

    printf("%d check(s) failed\n", fails);
    return fails;
}
