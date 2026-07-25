/* mupen64plus-VR — Tier-3B camera engine (packet S1 baseline; S10 scaffold + tests,
#include "vr_camera.h"
#include "vr_pose.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

#define VR_PI_F     3.14159265358979323846f
#define VR_RAD2DEG  57.29577951308232        /* 180/pi, double — S16_DEG scale */

/* ── quaternion → Euler (YXZ), radians ───────────────────────────────────────
static void vr_quat_to_euler_yxz(float x, float y, float z, float w,
                                 float* yaw, float* pitch, float* roll)
{
    const float n2 = x * x + y * y + z * z + w * w;
    if (!isfinite(n2) || n2 < 1e-12f) {
        return;
    }
    const float inv = 1.0f / sqrtf(n2);
    x *= inv; y *= inv; z *= inv; w *= inv;

    const float sinp = 2.0f * (w * x - y * z);
    if (fabsf(sinp) >= 1.0f)
    else


}


/* Round-to-nearest (half away from zero) with saturation to the s16 range,
static int32_t vr_round_sat_s16(double d)
{
    /* Clamp in DOUBLE before narrowing: lround() of an out-of-range double is UB, and
    if (d < (double)INT16_MIN) d = (double)INT16_MIN;
    if (d > (double)INT16_MAX) d = (double)INT16_MAX;
    return (int32_t)lround(d);
}

/* Splice a 16-bit value into the selected N64 halfword of `word`, preserving the
static uint32_t vr_splice_halfword(uint32_t word, uint32_t off, uint16_t half)
{
    if (off & 2u)
        return (word & 0xFFFF0000u) | (uint32_t)half;
    return (word & 0x0000FFFFu) | ((uint32_t)half << 16);
}

/* ── verified poke primitive ──────────────────────────────────────────────────
static int vr_dram_write32(uint32_t* dram, uint32_t dram_size_bytes, uint32_t addr, uint32_t val)
{
    uint32_t off = addr & 0xFFFFFF;               /* C1: mask first (accepts 0x80xxxxxx) */
    if (off + 4u <= dram_size_bytes) {            /* then bounds-check the physical word */
        dram[off >> 2] = val;
        return 1;
    }
    return 0;
}

static uint32_t vr_dram_read32(const uint32_t* dram, uint32_t dram_size_bytes, uint32_t addr, int* ok)
{
    uint32_t off = addr & 0xFFFFFF;
    if (off + 4u > dram_size_bytes) { *ok = 0; return 0; }
    return dram[off >> 2];
}

/* Resolve the camera base: deref cam_baseptr if set (must look like a KSEG0/KSEG1
static int vr_resolve_base(const uint32_t* dram, uint32_t dram_size_bytes,
                           const vr_profile* prof, uint32_t* out_base)
{
    if (prof->cam_baseptr != 0) {
        int ok = 0;
        uint32_t p = vr_dram_read32(dram, dram_size_bytes, prof->cam_baseptr, &ok);
        if (!ok || p < 0x80000000u || p > 0x807FFFFFu)
            return 0;
        return 1;
    }
    return 1;
}

/* Every guard must read back its `equals` value or the profile is disarmed. rel guards
static int vr_guards_pass(const uint32_t* dram, uint32_t dram_size_bytes,
                          const vr_profile* prof, uint32_t base)
{
    int i;
    for (i = 0; i < prof->n_guards; i++) {
        uint32_t addr = prof->guards[i].rel ? base + prof->guards[i].addr : prof->guards[i].addr;
        int ok = 0;
        uint32_t v = vr_dram_read32(dram, dram_size_bytes, addr, &ok);
        if (!ok || v != prof->guards[i].equals)
            return 0;
    }
    return 1;
}

void vr_camera_state_reset(vr_camera_state* st)
{
    if (st != NULL)
        memset(st, 0, sizeof *st);
}

int vr_camera_eval(uint32_t* dram, uint32_t dram_size_bytes,
                   const vr_profile* prof, const m64p_vr_pose* pose,
                   float sensitivity, vr_camera_state* st)
{
    float yaw, pitch, roll;
    uint32_t base = 0;
    int writes = 0;
    int i;

    if (dram == NULL || pose == NULL || !pose->valid || st == NULL)
        return 0;
    if (prof == NULL || prof->headlook_mode != VR_HL_MEMWRITE)
        return 0;
    if (!vr_resolve_base(dram, dram_size_bytes, prof, &base) ||
        !vr_guards_pass(dram, dram_size_bytes, prof, base)) {
        st->armed = 0;                          /* disarm; re-arm re-captures */
        return 0;
    }

    vr_quat_to_euler_yxz(pose->orient_x, pose->orient_y, pose->orient_z, pose->orient_w,
                         &yaw, &pitch, &roll);

    /* Arming edge: capture each compose-op's current game value and the head reference
    if (!st->armed) {
        for (i = 0; i < prof->n_ops && i < VR_MAX_OPS; i++) {
            const vr_cam_op* op = &prof->ops[i];
            float a = (op->axis == VR_AXIS_PITCH) ? pitch
                    : (op->axis == VR_AXIS_ROLL)  ? roll : yaw;
            if (op->unit == VR_UNIT_DEG)
                a *= (float)VR_RAD2DEG;
            st->head_ref[i] = a;
            st->base_val[i] = 0.0f;
            st->last_written[i] = 0.0f;
            if (op->mode == VR_MODE_COMPOSE || op->mode == VR_MODE_ANCHORED) {
                int ok = 0;
                uint32_t bits = vr_dram_read32(dram, dram_size_bytes, base + op->off, &ok);
                if (ok) {
                    float gv;
                    memcpy(&gv, &bits, sizeof gv);
                    if (isfinite(gv)) {
                        /* Arm VI: head_ref == current axis, so both modes write the
                        st->base_val[i] = gv;
                        st->last_written[i] = gv;
                    }
                }
            }
        }
        st->armed = 1;
    }

    for (i = 0; i < prof->n_ops; i++) {
        const vr_cam_op* op = &prof->ops[i];
        float axisval = (op->axis == VR_AXIS_PITCH) ? pitch
                      : (op->axis == VR_AXIS_ROLL)  ? roll
                                                    : yaw;   /* default/YAW */
        float val;
        uint32_t addr, off, bits;

        if (op->unit == VR_UNIT_DEG)
            axisval *= (float)VR_RAD2DEG;       /* head angle in the op's output units */

        if (op->mode == VR_MODE_COMPOSE && i < VR_MAX_OPS) {
            /* Shortest-arc the head offset (axis-ref) the same way DELTA does, THEN
            float off_h = axisval - st->head_ref[i];
            if (op->unit == VR_UNIT_DEG)
                off_h = remainderf(off_h, 360.0f);
            else
                off_h = remainderf(off_h, 2.0f * VR_PI_F);
            val = st->base_val[i] + op->scale * sensitivity * off_h + op->bias;
        }
        else if (op->mode == VR_MODE_DELTA && i < VR_MAX_OPS) {
            /* Live-value read-back below memcpy's the raw word straight into a float:
            if (op->encoding != VR_ENC_FLOAT32)
                continue;
            /* Delta (pcsx2-VR doc 07 §2 — the MouseInjector feel): add this VI's head
            int ok = 0;
            uint32_t gbits = vr_dram_read32(dram, dram_size_bytes, base + op->off, &ok);
            float game_now, d;
            d = axisval - st->head_ref[i];
            st->head_ref[i] = axisval;          /* consume the delta even if unreadable */
            if (op->unit == VR_UNIT_DEG)
                d = remainderf(d, 360.0f);      /* shortest arc across the atan2 seam */
            else
                d = remainderf(d, 2.0f * VR_PI_F);
            if (!ok)
                continue;
            memcpy(&game_now, &gbits, sizeof game_now);
            if (!isfinite(game_now))
                continue;
            val = game_now + op->scale * sensitivity * d;
        }
        else if (op->mode == VR_MODE_ANCHORED && i < VR_MAX_OPS) {
            /* Live-value read-back below is float32-only (see DELTA): an s16 encoding
            if (op->encoding != VR_ENC_FLOAT32)
                continue;
            /* Anchored (pcsx2-VR CameraDriver lineage): re-read the game each VI and
            int ok = 0;
            uint32_t gbits = vr_dram_read32(dram, dram_size_bytes, base + op->off, &ok);
            float game_now, own;
            if (!ok)
                continue;                       /* unreadable: keep state, no write */
            memcpy(&game_now, &gbits, sizeof game_now);
            if (!isfinite(game_now))
                continue;
            own = game_now - st->last_written[i];
            if (op->wrap == 360)
                own = remainderf(own, 360.0f);  /* shortest arc across the 0/360 seam */
            st->base_val[i] += own;
            if (op->wrap == 360)
                st->base_val[i] = remainderf(st->base_val[i], 360.0f);
            val = st->base_val[i] + op->scale * sensitivity * (axisval - st->head_ref[i]);
        }
        else
            val = op->bias + op->scale * sensitivity * axisval;

        if (val < op->clamp_lo) val = op->clamp_lo;
        if (val > op->clamp_hi) val = op->clamp_hi;
        if (op->wrap == 360) {
            val = fmodf(val, 360.0f);
            if (val < 0.0f) val += 360.0f;
        }
        if (!isfinite(val))
            continue;

        addr = base + op->off;
        off  = addr & 0xFFFFFF;                 /* C1: mask BEFORE the bounds check */
        if (off + 4u > dram_size_bytes)
            continue;                           /* unwritable => skip, don't count  */

        switch (op->encoding) {
        case VR_ENC_FLOAT32:
            memcpy(&bits, &val, sizeof bits);
            break;
        case VR_ENC_S16_RAD:
            bits = vr_splice_halfword(dram[off >> 2], off,
                       (uint16_t)(int16_t)vr_round_sat_s16((double)val * 4096.0));
            break;
        case VR_ENC_S16_DEG:
            bits = vr_splice_halfword(dram[off >> 2], off,
                       (uint16_t)(int16_t)vr_round_sat_s16((double)val * VR_RAD2DEG));
            break;
        default:
            continue;                           /* unknown encoding => skip */
        }

        if (op->mode == VR_MODE_ANCHORED && i < VR_MAX_OPS)
            st->last_written[i] = val;  /* post-clamp/wrap: what the game reads back next VI.
                                           Anchored is float32-only (guarded above), so the
                                           readback is exact and attribution stays drift-free.
                                           A non-f32 encoding would MISREAD the live word
                                           entirely — not quantize by <1 unit — hence the guard. */
        writes += vr_dram_write32(dram, dram_size_bytes, addr, bits);
    }
    return writes;
}

static int s_recenter_req = 0;

void vr_camera_request_recenter(void)
{
    s_recenter_req = 1;   /* consumed at the next production eval: forces re-arm,
                             which re-captures game values + head reference */
}

#ifndef VR_TESTING
#define M64P_CORE_PROTOTYPES 1          /* unlock the ConfigGetParamBool prototype */
#include "device/r4300/r4300_core.h"
#include "device/rdram/rdram.h"
#include "main/netplay.h"
#include "api/m64p_config.h"
#include "api/callbacks.h"
#include <stdlib.h>

extern m64p_handle g_VrConfig;   /* [Core-VR] section; defined in src/main/main.c */

/* MUPEN_VR_FAKE_HEADPOSE=<deg>: substitute a ±deg triangle-wave yaw sweep for the
static int vr_fake_pose(m64p_vr_pose* out)
{
    static int      inited  = 0;
    static int      enabled = 0;
    static float    amp_rad = 0.0f;
    static uint32_t counter = 0;

    if (!inited) {
        const char* e = getenv("MUPEN_VR_FAKE_HEADPOSE");
        inited = 1;
        if (e != NULL && *e != '\0') {
            enabled = 1;
            amp_rad = (float)atof(e) * (VR_PI_F / 180.0f);
        }
    }
    if (!enabled)
        return 0;

    {
        const uint32_t period = 240;                 /* ~4 s @ 60 VI/s */
        uint32_t n   = counter++;                    /* advance each call */
        float    t   = (float)(n % period) / (float)period;         /* [0,1)      */
        float    tri = (t < 0.5f) ? (-1.0f + 4.0f * t)              /* triangle   */
                                  : ( 3.0f - 4.0f * t);             /* in [-1,1]  */
        float    yaw = amp_rad * tri;
        float    half = yaw * 0.5f;

        memset(out, 0, sizeof *out);
        out->orient_y = sinf(half);                  /* pure rotation about +Y (yaw) */
        out->orient_w = cosf(half);
        out->valid    = 1;
        out->frame    = n + 1;
    }
    return 1;
}

static int vr_head_camera_enabled(void)
{
    /* The RESOLVED head-look trait (precedence applied at ROM open): the [Core-VR]
    return vr_settings_get()->head_look;
}

void vr_camera_apply(struct r4300_core* r4300)
{
    static vr_camera_state st;              /* zero-init: disarmed */
    m64p_vr_pose pose;
    const vr_profile* prof;

    if (!vr_head_camera_enabled())          /* Core-VR[EnableHeadCamera] master switch */
        return;
    if (netplay_is_init())                  /* C11: a RAM write here desyncs peers      */
        return;
    if (!vr_fake_pose(&pose)) {             /* FAKE sweep overrides the live pose       */
        if (!vr_pose_get(&pose))            /* real head pose; valid==0 => no-op        */
            return;
    }

    if (s_recenter_req) {                   /* recenter = force re-arm + re-capture     */
        s_recenter_req = 0;
        vr_camera_state_reset(&st);
    }

    prof = vr_profile_current();            /* NULL / non-memwrite => eval writes 0     */

    {
        /* MUPEN_VR_TRACE=1: [VRcam] arm/disarm edges + a 2 s heartbeat while armed —
        static int trace = -1;
        const int was_armed = st.armed;
        const int writes = vr_camera_eval(r4300->rdram->dram,
                                          (uint32_t)r4300->rdram->dram_size,
                                          prof, &pose,
                                          vr_settings_get()->head_look_sensitivity, &st);
        if (trace < 0) {
            const char* t = getenv("MUPEN_VR_TRACE");
            trace = (t != NULL && *t != '\0' && *t != '0') ? 1 : 0;
        }
        if (trace) {
            static uint32_t vi = 0;
            ++vi;
            if (st.armed != was_armed)
                DebugMessage(M64MSG_INFO, "[VRcam] %s (writes=%d)",
                             st.armed ? "ARMED - guards passed, game camera captured"
                                      : "disarmed - base/guard gate closed",
                             writes);
            else if (st.armed && (vi % 120u) == 0u)
                DebugMessage(M64MSG_INFO, "[VRcam] armed, writing (%d words/VI)", writes);
        }
    }
}
#endif /* VR_TESTING */
