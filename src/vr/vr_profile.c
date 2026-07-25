/* mupen64plus-VR — VR profile loader (packet S8).
 */
#include "vr_profile.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static vr_profile s_current;   /* valid=0 until loaded */

/* Compiled defaults — the base of the precedence chain, and the effective settings for
#define VR_DEFAULTS_INIT { 0.012f, 0.0f, 0, 1.0f, 2.0f, 1.4f, 0.0f, 0.0f }
static vr_settings s_effective = VR_DEFAULTS_INIT;   /* resolved; vr_settings_get() */

/* deg->rad; a clampdeg key is stored in radians because the write value
#define VR_DEG2RAD    0.017453292519943295f
#define VR_CLAMP_INF  1.0e30f


static void vr_str_copy(char* dst, size_t dstsz, const char* src)
{
    size_t i = 0;
    if (dstsz == 0)
        return;
    for (; i + 1 < dstsz && src[i] != '\0'; ++i)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* strtof with strict error checking. Trailing whitespace tolerated (CRLF-safe),
static int vr_parse_f32(const char* s, float* out)
{
    char* endp = NULL;
    float v;
    errno = 0;
    v = strtof(s, &endp);
    if (endp == s)
        return 1;                       /* no digits consumed */
    while (*endp != '\0' && isspace((unsigned char)*endp))
        ++endp;
    if (*endp != '\0')
        return 1;                       /* trailing garbage */
    return 0;
}

static int vr_parse_u32(const char* s, uint32_t* out)
{
    char* endp = NULL;
    unsigned long v;
    errno = 0;
    v = strtoul(s, &endp, 0);
    if (endp == s)
        return 1;                       /* no digits consumed */
    while (*endp != '\0' && isspace((unsigned char)*endp))
        ++endp;
    if (*endp != '\0')
        return 1;                       /* trailing garbage */
    if (errno == ERANGE)
        return 1;                       /* out of unsigned long range */
    return 0;
}

static int vr_eq_ci(const char* a, const char* b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

static int vr_parse_bool(const char* s, int* out)
{
    if (vr_eq_ci(s, "true") || vr_eq_ci(s, "1") || vr_eq_ci(s, "on")  || vr_eq_ci(s, "yes")) { *out = 1; return 0; }
    if (vr_eq_ci(s, "false")|| vr_eq_ci(s, "0") || vr_eq_ci(s, "off") || vr_eq_ci(s, "no"))  { *out = 0; return 0; }
    return 1;
}

static int vr_set_f32(vr_profile* p, uint32_t bit, float* field, const char* value)
{
    if (vr_parse_f32(value, field)) return 1;
    p->present |= bit;
    return 0;
}

/* Find the slot-th ops[] entry for an axis (slot 0 = cam.*, slot 1 = cam2.*), or
static vr_cam_op* vr_find_or_add_op(vr_profile* p, int axis, int slot)
{
    int i, seen = 0;
    vr_cam_op* op;
    for (i = 0; i < p->n_ops; ++i)
        if (p->ops[i].axis == (uint8_t)axis && seen++ == slot)
            return &p->ops[i];
    if (p->n_ops >= VR_MAX_OPS)
        return NULL;
    op = &p->ops[p->n_ops++];
    op->off      = 0;
    op->scale    = 1.0f;               /* identity until cam.<axis>.scale sets it */
    op->bias     = 0.0f;
    op->clamp_lo = -VR_CLAMP_INF;      /* effectively unclamped until clampdeg */
    op->clamp_hi =  VR_CLAMP_INF;
    op->axis     = (uint8_t)axis;
    op->encoding = VR_ENC_FLOAT32;     /* schema v1: float32 at base+off */
    op->unit     = VR_UNIT_RAD;        /* radians unless cam.<axis>.unit=deg */
    op->mode     = VR_MODE_ABS;        /* absolute unless cam.<axis>.mode=compose */
    op->wrap     = 0;
    return op;
}

/* Match "<prefix><digits>." at the head of key. On success set *idx (in range
static int vr_parse_indexed(const char* key, const char* prefix, int maxn,
                            long* idx, const char** field)
{
    size_t plen = strlen(prefix);
    const char* p;
    char* endp = NULL;
    long i;
    if (strncmp(key, prefix, plen) != 0)
        return 0;
    p = key + plen;
    i = strtol(p, &endp, 10);
    if (endp == p || *endp != '.')
        return 0;                       /* missing/garbled index */
    if (i < 0 || i >= maxn)
        return 0;                       /* out of range -> unknown */
    return 1;
}


const vr_profile* vr_profile_current(void)
{
    return s_current.valid ? &s_current : NULL;
}

const vr_settings* vr_settings_get(void)
{
    return &s_effective;
}

/* One USER-OWNED field: substitute-when-default. Profile fills the knob; a deliberate
static float vr_resolve_user_owned(uint32_t bit, float def, float prof, uint32_t pp,
                                   float global, float ovr, uint32_t op)
{
    float v = def;
    if (pp & bit)         v = prof;
    if (global != def)    v = global;
    if (op & bit)         v = ovr;
    return v;
}

void vr_profile_resolve(const vr_settings* def, const vr_profile* prof,
                        const vr_settings* global, const vr_profile* ovr,
                        vr_settings* out)
{
    uint32_t pp = prof ? prof->present : 0u;
    uint32_t op = ovr  ? ovr->present  : 0u;
    float one_minus;

    /* User-owned class — substitute-when-default. Includes screen_vertical_offset: it is
    out->separation = vr_resolve_user_owned(VR_F_SEPARATION, def->separation,
        prof ? prof->separation : 0.0f, pp, global->separation, ovr ? ovr->separation : 0.0f, op);
    out->convergence = vr_resolve_user_owned(VR_F_CONVERGENCE, def->convergence,
        prof ? prof->convergence : 0.0f, pp, global->convergence, ovr ? ovr->convergence : 0.0f, op);
    out->head_look_sensitivity = vr_resolve_user_owned(VR_F_HEAD_LOOK_SENSITIVITY, def->head_look_sensitivity,
        prof ? prof->head_look_sensitivity : 0.0f, pp, global->head_look_sensitivity,
        ovr ? ovr->head_look_sensitivity : 0.0f, op);
    out->screen_vertical_offset = vr_resolve_user_owned(VR_F_SCREEN_VERTICAL_OFFSET,
        def->screen_vertical_offset, prof ? prof->screen_vertical_offset : 0.0f, pp,
        global->screen_vertical_offset, ovr ? ovr->screen_vertical_offset : 0.0f, op);

    /* Game-trait class — authoritative-when-specified (the user global is the base/comfort
    out->head_look = global->head_look;
    if (pp & VR_F_HEAD_LOOK) out->head_look = prof->head_look;
    if (op & VR_F_HEAD_LOOK) out->head_look = ovr->head_look;

    out->screen_distance = global->screen_distance;
    if (pp & VR_F_SCREEN_DISTANCE) out->screen_distance = prof->screen_distance;
    if (op & VR_F_SCREEN_DISTANCE) out->screen_distance = ovr->screen_distance;

    out->screen_height = global->screen_height;
    if (pp & VR_F_SCREEN_HEIGHT) out->screen_height = prof->screen_height;
    if (op & VR_F_SCREEN_HEIGHT) out->screen_height = ovr->screen_height;

    out->screen_arc = global->screen_arc;
    if (pp & VR_F_SCREEN_ARC) out->screen_arc = prof->screen_arc;
    if (op & VR_F_SCREEN_ARC) out->screen_arc = ovr->screen_arc;

    if (out->separation < 0.0f)    out->separation = 0.0f;
    if (out->separation > 0.1f)    out->separation = 0.1f;
    if (out->convergence < 0.0f)       out->convergence = 0.0f;
    /* Convergence is in RAW CLIP-W units (NOT normalized [0,1]); measured calibrations
    if (out->convergence > 65536.0f)   out->convergence = 65536.0f;
    if (out->head_look_sensitivity < 0.1f) out->head_look_sensitivity = 0.1f;
    if (out->head_look_sensitivity > 2.0f) out->head_look_sensitivity = 2.0f;
    out->head_look = out->head_look ? 1 : 0;
    /* Screen geometry: keep the quad non-degenerate (in front of the eye, visible size)
    if (out->screen_distance < 0.2f)  out->screen_distance = 0.2f;
    if (out->screen_distance > 10.0f) out->screen_distance = 10.0f;
    if (out->screen_height < 0.2f)    out->screen_height = 0.2f;
    if (out->screen_height > 5.0f)    out->screen_height = 5.0f;
    if (out->screen_arc < 0.0f)       out->screen_arc = 0.0f;
    if (out->screen_arc > 360.0f)     out->screen_arc = 360.0f;
    if (out->screen_vertical_offset < -2.0f) out->screen_vertical_offset = -2.0f;
    if (out->screen_vertical_offset >  2.0f) out->screen_vertical_offset =  2.0f;

    /* Comfort ceiling: separation*(1-convergence) <= VR_COMFORT_CEILING. Only meaningful
    one_minus = 1.0f - out->convergence;
    if (out->convergence >= 0.0f && out->convergence <= 1.0f && one_minus > 1e-6f) {
        if (out->separation * one_minus > VR_COMFORT_CEILING)
            out->separation = VR_COMFORT_CEILING / one_minus;
    }
}

int vr_profile_apply_kv(vr_profile* p, const char* key, const char* value)
{
    if (p == NULL || key == NULL || value == NULL)
        return 1;

    if (vr_eq_ci(key, "name")) {
        vr_str_copy(p->name, sizeof(p->name), value);
        return 0;
    }
    if (vr_eq_ci(key, "genre")) {
        vr_str_copy(p->genre, sizeof(p->genre), value);
        return 0;
    }
    if (vr_eq_ci(key, "notes"))         /* documentation only; parsed + ignored */
        return 0;
    if (vr_eq_ci(key, "calibration")) {
        if      (vr_eq_ci(value, "uncalibrated")) p->calibration = VR_CAL_UNCALIBRATED;
        else if (vr_eq_ci(value, "community"))    p->calibration = VR_CAL_COMMUNITY;
        else if (vr_eq_ci(value, "measured"))     p->calibration = VR_CAL_MEASURED;
        else return 1;
        return 0;
    }

    if (vr_eq_ci(key, "separation") || strcmp(key, "stereo.sep") == 0)
        return vr_set_f32(p, VR_F_SEPARATION, &p->separation, value);
    if (vr_eq_ci(key, "convergence") || strcmp(key, "stereo.conv") == 0)
        return vr_set_f32(p, VR_F_CONVERGENCE, &p->convergence, value);

    if (vr_eq_ci(key, "headLook")) {
        if (vr_parse_bool(value, &p->head_look)) return 1;
        p->present |= VR_F_HEAD_LOOK;
        return 0;
    }
    if (vr_eq_ci(key, "headLookSensitivity") || strcmp(key, "headlook.gain") == 0)
        return vr_set_f32(p, VR_F_HEAD_LOOK_SENSITIVITY, &p->head_look_sensitivity, value);
    if (strcmp(key, "headlook.mechanism") == 0 || strcmp(key, "headlook.mode") == 0) {
        if      (vr_eq_ci(value, "none"))     p->headlook_mode = VR_HL_NONE;
        else if (vr_eq_ci(value, "matrix"))   p->headlook_mode = VR_HL_MATRIX;
        else if (vr_eq_ci(value, "memwrite")) p->headlook_mode = VR_HL_MEMWRITE;
        else if (vr_eq_ci(value, "padlook"))  p->headlook_mode = VR_HL_PADLOOK;
        else return 1;                  /* unrecognized mechanism -> unchanged */
        return 0;
    }

    if (vr_eq_ci(key, "screenDistance"))
        return vr_set_f32(p, VR_F_SCREEN_DISTANCE, &p->screen_distance, value);
    if (vr_eq_ci(key, "screenHeight"))
        return vr_set_f32(p, VR_F_SCREEN_HEIGHT, &p->screen_height, value);
    if (vr_eq_ci(key, "screenArc"))
        return vr_set_f32(p, VR_F_SCREEN_ARC, &p->screen_arc, value);
    if (vr_eq_ci(key, "screenVerticalOffset"))
        return vr_set_f32(p, VR_F_SCREEN_VERTICAL_OFFSET, &p->screen_vertical_offset, value);

    if (strcmp(key, "cam.base") == 0)
        return vr_parse_u32(value, &p->cam_base);
    if (strcmp(key, "cam.baseptr") == 0)
        return vr_parse_u32(value, &p->cam_baseptr);

    /* ---- camera ops: cam.<axis>.<field> / cam2.<axis>.<field> (second write
    if (strncmp(key, "cam.", 4) == 0 || strncmp(key, "cam2.", 5) == 0) {
        const int slot = (key[3] == '2') ? 1 : 0;
        const char* rest = key + (slot ? 5 : 4);
        const char* dot  = strchr(rest, '.');
        const char* field;
        size_t alen;
        int axis;
        vr_cam_op* op;
        if (dot == NULL)
            return 0;                   /* unknown cam.* key */
        alen = (size_t)(dot - rest);
        if      (alen == 3 && strncmp(rest, "yaw",   3) == 0) axis = VR_AXIS_YAW;
        else if (alen == 5 && strncmp(rest, "pitch", 5) == 0) axis = VR_AXIS_PITCH;
        else if (alen == 4 && strncmp(rest, "roll",  4) == 0) axis = VR_AXIS_ROLL;
        else return 0;                  /* unknown axis -> unknown key */
        field = dot + 1;

        if (strcmp(field, "off") == 0) {
            uint32_t v;
            if (vr_parse_u32(value, &v)) return 1;
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            op->off = v;
            return 0;
        }
        if (strcmp(field, "scale") == 0) {
            float v;
            if (vr_parse_f32(value, &v)) return 1;
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            op->scale = v;
            return 0;
        }
        if (strcmp(field, "bias") == 0) {
            float v;
            if (vr_parse_f32(value, &v)) return 1;
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            op->bias = v;
            return 0;
        }
        if (strcmp(field, "clampdeg") == 0) {
            float deg;
            if (vr_parse_f32(value, &deg)) return 1;
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            /* Clamp is stored (and applied by the engine) in the op's OUTPUT units:
            op->clamp_hi = (op->unit == VR_UNIT_DEG) ? deg : deg * VR_DEG2RAD;
            op->clamp_lo = -op->clamp_hi;
            return 0;
        }
        if (strcmp(field, "unit") == 0) {
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            if      (vr_eq_ci(value, "rad")) op->unit = VR_UNIT_RAD;
            else if (vr_eq_ci(value, "deg")) op->unit = VR_UNIT_DEG;
            else return 1;
            return 0;
        }
        if (strcmp(field, "mode") == 0) {
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            if      (vr_eq_ci(value, "abs"))      op->mode = VR_MODE_ABS;
            else if (vr_eq_ci(value, "compose"))  op->mode = VR_MODE_COMPOSE;
            else if (vr_eq_ci(value, "anchored")) op->mode = VR_MODE_ANCHORED;
            else if (vr_eq_ci(value, "delta"))    op->mode = VR_MODE_DELTA;
            else return 1;
            return 0;
        }
        if (strcmp(field, "wrap") == 0) {
            uint32_t w;
            if (vr_parse_u32(value, &w)) return 1;
            if (w != 0 && w != 360) return 1;
            op = vr_find_or_add_op(p, axis, slot);
            if (op == NULL) return 1;
            op->wrap = (uint16_t)w;
            return 0;
        }
        return 0;                       /* unknown cam field -> ignore */
    }

    {
        long idx;
        const char* field;
        if (vr_parse_indexed(key, "guard.", VR_MAX_GUARDS, &idx, &field)) {
            uint32_t v;
            if (strcmp(field, "addr") == 0) {
                if (vr_parse_u32(value, &v)) return 1;
                p->guards[idx].addr = v;
            } else if (strcmp(field, "equals") == 0) {
                if (vr_parse_u32(value, &v)) return 1;
                p->guards[idx].equals = v;
            } else if (strcmp(field, "rel") == 0) {
                if (vr_parse_u32(value, &v)) return 1;
                p->guards[idx].rel = (uint8_t)(v != 0);
            } else {
                return 0;               /* unknown guard field */
            }
            if ((int)(idx + 1) > p->n_guards)
                p->n_guards = (int)(idx + 1);
            return 0;
        }
    }

    {
        long idx;
        const char* field;
        if (vr_parse_indexed(key, "silence.", VR_MAX_SILENCE, &idx, &field)) {
            uint32_t v;
            if (strcmp(field, "addr") == 0) {
                if (vr_parse_u32(value, &v)) return 1;
                p->silence[idx].addr = v;
            } else if (strcmp(field, "patch") == 0) {
                if (vr_parse_u32(value, &v)) return 1;
                p->silence[idx].patch = v;
            } else {
                return 0;               /* unknown silence field */
            }
            if ((int)(idx + 1) > p->n_silence)
                p->n_silence = (int)(idx + 1);
            return 0;
        }
    }

    return 0;                           /* unknown key -> ignore */
}

#ifndef VR_TESTING

#include <stdio.h>

/* Unlock the EXPORT ... CALL Config* / DebugMessage prototypes in the API
#define M64P_CORE_PROTOTYPES 1
#include "api/m64p_types.h"
#include "api/m64p_config.h"
#include "api/callbacks.h"
#include "main/rom.h"

/* Truncate an in-value inline comment (" ; ..." / " # ...") so an activated
static void vr_strip_inline_comment(char* s)
{
    char* c = s;
    while (*c != '\0' && *c != ';' && *c != '#')
        ++c;
    while (c > s && (c[-1] == ' ' || c[-1] == '\t'))
}

/* Query one key of [section] and apply it to `into`. Returns 1 iff a value was
static int vr_try_key(m64p_handle cfg, const char* section, const char* key, vr_profile* into)
{
    char buf[256];
    if (ConfigExternalGetParameter(cfg, section, key, buf, (int)sizeof(buf)) != M64ERR_SUCCESS)
        return 0;                       /* key absent in this section */
    buf[sizeof(buf) - 1] = '\0';        /* ConfigExternalGetParameter may not NUL */
    vr_strip_inline_comment(buf);
    if (vr_profile_apply_kv(into, key, buf) != 0) {
        DebugMessage(M64MSG_WARNING, "VR profile: bad value for '%s' = '%s' (skipped)", key, buf);
        return 0;
    }
    return 1;
}

/* Every known key, queried by name (ConfigExternal gets; it does not enumerate). Canonical
static const char* const VR_SCALAR_KEYS[] = {
    "name", "genre", "calibration",
    "separation", "convergence",
    "headLook", "headLookSensitivity", "headlook.mechanism",
    "screenDistance", "screenHeight", "screenArc", "screenVerticalOffset",
    "stereo.sep", "stereo.conv", "headlook.mode", "headlook.gain",   /* legacy aliases */
    "cam.base", "cam.baseptr",
    "cam.yaw.off",   "cam.yaw.unit",   "cam.yaw.mode",   "cam.yaw.wrap",
    "cam.yaw.scale", "cam.yaw.bias",   "cam.yaw.clampdeg",
    "cam.pitch.off",   "cam.pitch.unit", "cam.pitch.mode", "cam.pitch.wrap",
    "cam.pitch.scale", "cam.pitch.bias", "cam.pitch.clampdeg",
    "cam.roll.off",   "cam.roll.unit",  "cam.roll.mode",  "cam.roll.wrap",
    "cam.roll.scale", "cam.roll.bias",  "cam.roll.clampdeg",
    "cam2.yaw.off",   "cam2.yaw.unit",   "cam2.yaw.mode",   "cam2.yaw.wrap",
    "cam2.yaw.scale", "cam2.yaw.bias",   "cam2.yaw.clampdeg",
    "cam2.pitch.off",   "cam2.pitch.unit", "cam2.pitch.mode", "cam2.pitch.wrap",
    "cam2.pitch.scale", "cam2.pitch.bias", "cam2.pitch.clampdeg",
    "cam2.roll.off",   "cam2.roll.unit",  "cam2.roll.mode",  "cam2.roll.wrap",
    "cam2.roll.scale", "cam2.roll.bias",  "cam2.roll.clampdeg",
};

static int vr_load_profile_from(m64p_handle cfg, const char* section, vr_profile* into)
{
    int matched = 0, n;
    size_t i;
    char key[64];
    for (i = 0; i < sizeof(VR_SCALAR_KEYS) / sizeof(VR_SCALAR_KEYS[0]); ++i)
        matched += vr_try_key(cfg, section, VR_SCALAR_KEYS[i], into);
    for (n = 0; n < VR_MAX_GUARDS; ++n) {
        snprintf(key, sizeof(key), "guard.%d.addr", n);   matched += vr_try_key(cfg, section, key, into);
        snprintf(key, sizeof(key), "guard.%d.equals", n); matched += vr_try_key(cfg, section, key, into);
        snprintf(key, sizeof(key), "guard.%d.rel", n);    matched += vr_try_key(cfg, section, key, into);
    }
    for (n = 0; n < VR_MAX_SILENCE; ++n) {
        snprintf(key, sizeof(key), "silence.%d.addr", n);  matched += vr_try_key(cfg, section, key, into);
        snprintf(key, sizeof(key), "silence.%d.patch", n); matched += vr_try_key(cfg, section, key, into);
    }
    return matched;
}

/* Open a profile ini by name and load [section] into `into`. Returns 0 if absent.
static int vr_load_profile_file(const char* filename, const char* section, vr_profile* into)
{
    const char* path = ConfigGetSharedDataFilepath(filename);
    char userpath[1024];
    m64p_handle cfg = NULL;
    int matched;
    if (path == NULL) {
        const char* udata = ConfigGetUserDataPath();
        size_t len;
        if (udata == NULL)
            return 0;
        len = strlen(udata);
        snprintf(userpath, sizeof(userpath), "%s%s%s",
                 udata, (len > 0 && udata[len - 1] == '/') ? "" : "/", filename);
        path = userpath;
    }
    if (ConfigExternalOpen(path, &cfg) != M64ERR_SUCCESS)
        return 0;                       /* file absent -> that layer specifies nothing */
    matched = vr_load_profile_from(cfg, section, into);
    ConfigExternalClose(cfg);
    return matched;
}

static const vr_settings VR_DEFAULTS = VR_DEFAULTS_INIT;

/* The user's global VR knobs live in [Core-VR]; read them into `g` starting from the
extern m64p_handle g_VrConfig;   /* [Core-VR] section; defined in src/main/main.c */
static void vr_read_global_settings(vr_settings* g)
{
    if (g_VrConfig == NULL)
        return;
    g->separation             = ConfigGetParamFloat(g_VrConfig, "Separation");
    g->convergence            = ConfigGetParamFloat(g_VrConfig, "Convergence");
    g->head_look              = ConfigGetParamBool (g_VrConfig, "EnableHeadCamera");
    g->head_look_sensitivity  = ConfigGetParamFloat(g_VrConfig, "HeadLookSensitivity");
    g->screen_distance        = ConfigGetParamFloat(g_VrConfig, "ScreenDistance");
    g->screen_height          = ConfigGetParamFloat(g_VrConfig, "ScreenHeight");
    g->screen_arc             = ConfigGetParamFloat(g_VrConfig, "ScreenArc");
    g->screen_vertical_offset = ConfigGetParamFloat(g_VrConfig, "ScreenVerticalOffset");
}

void vr_profile_load_for_current_rom(void)
{
    const char* md5;
    vr_settings global;
    vr_profile  ovrp;               /* per-game user override (vr-overrides.ini) */
    int matched, ovr_matched;

    memset(&s_current, 0, sizeof(s_current));
    memset(&ovrp,      0, sizeof(ovrp));

    md5 = ROM_SETTINGS.MD5;         /* char[33] uppercase hex; sections case-insensitive */

    matched     = vr_load_profile_file("vr-profiles.ini",  md5, &s_current);
    ovr_matched = vr_load_profile_file("vr-overrides.ini", md5, &ovrp);

    if (matched > 0) {
        vr_str_copy(s_current.md5, sizeof(s_current.md5), md5);
        s_current.valid = 1;
        DebugMessage(M64MSG_INFO, "VR profile: loaded '%s' for MD5 %s (%d key(s))",
                     s_current.name[0] ? s_current.name : "(unnamed)", md5, matched);
    } else {
        /* Say so out loud: a silent miss is indistinguishable from a hit whose values
        DebugMessage(M64MSG_INFO, "VR profile: no entry for MD5 %s (global/default settings)", md5);
    }

    /* Resolve effective settings: compiled default < profile < [Core-VR] global < per-game
    vr_read_global_settings(&global);
    vr_profile_resolve(&VR_DEFAULTS, matched > 0 ? &s_current : NULL,
                       &global, ovr_matched > 0 ? &ovrp : NULL, &s_effective);

    DebugMessage(M64MSG_INFO,
        "VR settings: sep=%.4f conv=%.3f headLook=%d sens=%.2f screen(d=%.2f h=%.2f arc=%.1f voff=%.2f)",
        s_effective.separation, s_effective.convergence, s_effective.head_look,
        s_effective.head_look_sensitivity, s_effective.screen_distance, s_effective.screen_height,
        s_effective.screen_arc, s_effective.screen_vertical_offset);
}

/* Exported for the video plugin (api_export.ver): the resolved per-game stereo depth.
EXPORT void CALL CoreVR_GetStereo(float* separation, float* convergence)
{
    if (separation  != NULL) *separation  = s_effective.separation;
    if (convergence != NULL) *convergence = s_effective.convergence;
}
#endif /* VR_TESTING */
