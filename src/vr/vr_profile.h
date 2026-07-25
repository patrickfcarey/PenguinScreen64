/* mupen64plus-VR — per-game VR profile (packet S1 contract; S8 owns the loader).
 */
#ifndef M64P_VR_PROFILE_H
#define M64P_VR_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* headlook_mode is the head-look MECHANISM (how head rotation reaches the game),
enum vr_headlook_mode { VR_HL_NONE = 0, VR_HL_MATRIX, VR_HL_MEMWRITE, VR_HL_PADLOOK };
enum vr_axis         { VR_AXIS_YAW = 0, VR_AXIS_PITCH, VR_AXIS_ROLL };
enum vr_encoding     { VR_ENC_FLOAT32 = 0, VR_ENC_S16_RAD, VR_ENC_S16_DEG /* extend in S8 */ };
enum vr_unit         { VR_UNIT_RAD = 0, VR_UNIT_DEG };
enum vr_op_mode      { VR_MODE_ABS = 0,   /* out = bias + scale*axis                  */
                       VR_MODE_COMPOSE,   /* out = game_value@arm + scale*(axis-ref@arm) + bias */
                       VR_MODE_ANCHORED,  /* out = live_baseline + scale*(axis-ref@arm); the
                                             baseline re-reads the game each VI and folds the
                                             game's OWN turning (stick input) in 1:1. For engines
                                             that RATE-LIMIT the view angle (pcsx2-VR's KF4
                                             lesson) — on plain integrator FPS engines the
                                             baseline also absorbs the game's smoothing response
                                             to OUR writes and the view rubber-bands. */
                       VR_MODE_DELTA };   /* out = live_value + scale*(axis_now - axis_prev):
                                             read-modify-write, per-VI head DELTAS added to
                                             whatever is there — the MouseInjector/mouse feel.
                                             THE mechanism for rate-stick integrator FPS
                                             (GE/PD/TimeSplitters class; pcsx2-VR doc 07 §2).
                                             Stick keeps working underneath; clamps behave like
                                             a mouse; recenter is naturally a no-op.
                                             (auto-recenters when the profile arms)     */
enum vr_calibration  { VR_CAL_UNCALIBRATED = 0, VR_CAL_COMMUNITY, VR_CAL_MEASURED };

/* Which framework fields a profile/override layer explicitly specified (the `present`
enum {
    VR_F_SEPARATION             = 1u << 0,
    VR_F_CONVERGENCE            = 1u << 1,
    VR_F_HEAD_LOOK              = 1u << 2,
    VR_F_HEAD_LOOK_SENSITIVITY  = 1u << 3,
    VR_F_SCREEN_DISTANCE        = 1u << 4,
    VR_F_SCREEN_HEIGHT          = 1u << 5,
    VR_F_SCREEN_ARC             = 1u << 6,
    VR_F_SCREEN_VERTICAL_OFFSET = 1u << 7,
};
#define VR_F_USER_OWNED_CLASS (VR_F_SEPARATION | VR_F_CONVERGENCE | \
                               VR_F_HEAD_LOOK_SENSITIVITY | VR_F_SCREEN_VERTICAL_OFFSET)
#define VR_F_TRAIT_CLASS      (VR_F_HEAD_LOOK | VR_F_SCREEN_DISTANCE | \
                               VR_F_SCREEN_HEIGHT | VR_F_SCREEN_ARC)

/* Comfort ceiling on at-infinity background parallax: separation*(1-convergence) <= this.
#define VR_COMFORT_CEILING  0.031f

#define VR_MAX_OPS      8
#define VR_MAX_GUARDS   8
#define VR_MAX_SILENCE  8

/* One camera write op: base+off <- encode(wrap(clamp(value))), where value is computed
typedef struct {
    uint32_t off;        /* offset from the resolved base (or absolute if base==0) */
    float    scale, bias;
    float    clamp_lo, clamp_hi;  /* applied to the FINAL value, in `unit` units */
    uint8_t  axis;       /* enum vr_axis */
    uint8_t  encoding;   /* enum vr_encoding */
    uint8_t  unit;       /* enum vr_unit: rad (default) or deg — head angle conversion */
    uint8_t  mode;       /* enum vr_op_mode: abs (default) or compose (capture-on-arm) */
    uint16_t wrap;       /* 0 = none; 360 = wrap final value into [0,360) (deg yaw)    */
} vr_cam_op;

typedef struct {
    uint32_t addr, equals;
    uint8_t  rel;        /* 1 = addr is an offset from the resolved camera base */
} vr_guard;              /* arm iff *(base-rel or absolute addr) == equals */
typedef struct { uint32_t addr, patch, saved; uint8_t armed; } vr_silence; /* writer-silence */

typedef struct {
    int      valid;                 /* 0 => no profile for the running ROM */
    char     md5[33];               /* uppercase hex key */
    char     name[64];
    char     genre[48];             /* documentation; also drives seed reasoning */
    int      calibration;           /* enum vr_calibration (provenance) */
    uint32_t present;               /* bitmask of VR_F_* fields this layer specified */

    /* Group 1 — stereo depth. separation is the parallax budget (near-global comfort
    float    separation, convergence;

    /* Group 2 — head-look. head_look is the authoritative on/off TRAIT; headlook_mode is
    int      head_look;             /* 0/1 (only meaningful if present & VR_F_HEAD_LOOK) */
    int      headlook_mode;         /* enum vr_headlook_mode (mechanism) */
    float    head_look_sensitivity;

    float    screen_distance;       /* metres in front of the viewer */
    float    screen_height;         /* physical screen size, metres */
    float    screen_arc;            /* cylindrical wrap, degrees (0 = flat quad) */
    float    screen_vertical_offset;/* +up / -down vs the recenter eye-height anchor, m */

    /* Head-look MEMWRITE binding (the pcsx2-analog per-game memory-camera pattern). Only
    uint32_t cam_base;              /* static base (used when cam_baseptr == 0) */
    uint32_t cam_baseptr;           /* if nonzero: base = *(u32*)cam_baseptr each VI
                                       (dynamic player block, e.g. GE 0x8007A0B0);
                                       disarms unless the value looks like 0x80xxxxxx */
    vr_cam_op  ops[VR_MAX_OPS];        int n_ops;
    vr_guard   guards[VR_MAX_GUARDS];  int n_guards;
    vr_silence silence[VR_MAX_SILENCE];int n_silence;
} vr_profile;

/* Resolved, effective VR settings after precedence (vr_profile_resolve output). These are
typedef struct {
    float separation, convergence;
    int   head_look;
    float head_look_sensitivity;
    float screen_distance, screen_height, screen_arc, screen_vertical_offset;
} vr_settings;

/* Precedence (framework doc 09 §3): compiled default < profile < user global < per-game
void vr_profile_resolve(const vr_settings* defaults, const vr_profile* profile,
                        const vr_settings* global,   const vr_profile* override,
                        vr_settings* out);

const vr_profile* vr_profile_current(void);

/* The resolved effective settings for the running ROM (compiled defaults if nothing loaded).
const vr_settings* vr_settings_get(void);

/* PURE + testable: apply one dotted key ("stereo.sep", "cam.yaw.off", ...) with its
int vr_profile_apply_kv(vr_profile* p, const char* key, const char* value);

#ifndef VR_TESTING
/* Load the profile for the currently-open ROM (keyed by ROM_SETTINGS.MD5) from
void vr_profile_load_for_current_rom(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_PROFILE_H */
