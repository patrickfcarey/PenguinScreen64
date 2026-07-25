/* mupen64plus-VR — profile-parse unit tests (packet S8). Owns this file.
#include "vr_tests.h"
#include "vr/vr_profile.h"

#include <math.h>
#include <string.h>

static int approx(float a, float b) { return fabsf(a - b) < 1e-4f; }

int test_profile(void)
{
    int fails = 0;
    vr_profile p;

    memset(&p, 0, sizeof(p));
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "name", "GoldenEye 007 (U)") == 0, "name applies");
    VR_CHECK(&fails, strcmp(p.name, "GoldenEye 007 (U)") == 0, "name copied");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "stereo.sep", "0.012") == 0, "stereo.sep applies");
    VR_CHECK(&fails, approx(p.separation, 0.012f), "stereo.sep -> separation");
    VR_CHECK(&fails, (p.present & VR_F_SEPARATION) != 0, "stereo.sep sets present bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "stereo.conv", "12.0") == 0, "stereo.conv applies");
    VR_CHECK(&fails, approx(p.convergence, 12.0f), "stereo.conv -> convergence");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.gain", "1.5") == 0, "headlook.gain applies");
    VR_CHECK(&fails, approx(p.head_look_sensitivity, 1.5f), "headlook.gain -> head_look_sensitivity");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.base", "0x8007ABCD") == 0, "cam.base applies");
    VR_CHECK(&fails, p.cam_base == 0x8007ABCDu, "cam.base hex (strtoul base 0)");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.base", "16") == 0 && p.cam_base == 16u, "cam.base decimal");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "none") == 0     && p.headlook_mode == VR_HL_NONE,     "mode none");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "matrix") == 0   && p.headlook_mode == VR_HL_MATRIX,   "mode matrix");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "memwrite") == 0 && p.headlook_mode == VR_HL_MEMWRITE, "mode memwrite");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "padlook") == 0  && p.headlook_mode == VR_HL_PADLOOK,  "mode padlook");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "MemWrite") == 0 && p.headlook_mode == VR_HL_MEMWRITE, "mode case-insensitive");

    memset(&p, 0, sizeof(p));
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.yaw.off", "0x0C") == 0, "cam.yaw.off applies");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.yaw.scale", "-1.0") == 0, "cam.yaw.scale applies");
    VR_CHECK(&fails, p.n_ops == 1, "both yaw keys map to one op");
    VR_CHECK(&fails, p.ops[0].axis == VR_AXIS_YAW, "op0 axis = yaw");
    VR_CHECK(&fails, p.ops[0].off == 0x0Cu, "op0 off = 0x0C");
    VR_CHECK(&fails, approx(p.ops[0].scale, -1.0f), "op0 scale = -1.0");
    VR_CHECK(&fails, p.ops[0].encoding == VR_ENC_FLOAT32, "op0 encoding = float32");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.pitch.off", "0x10") == 0, "cam.pitch.off applies");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.pitch.clampdeg", "85") == 0, "cam.pitch.clampdeg applies");
    VR_CHECK(&fails, p.n_ops == 2, "pitch keys add a second op");
    VR_CHECK(&fails, p.ops[1].axis == VR_AXIS_PITCH, "op1 axis = pitch");
    VR_CHECK(&fails, p.ops[1].off == 0x10u, "op1 off = 0x10");
    VR_CHECK(&fails, approx(p.ops[1].clamp_hi,  85.0f * 0.017453292519943295f), "op1 clamp_hi = 85deg in rad");
    VR_CHECK(&fails, approx(p.ops[1].clamp_lo, -85.0f * 0.017453292519943295f), "op1 clamp_lo symmetric");

    memset(&p, 0, sizeof(p));
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "guard.0.addr", "0x80112233") == 0, "guard.0.addr applies");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "guard.0.equals", "0x00000002") == 0, "guard.0.equals applies");
    VR_CHECK(&fails, p.guards[0].addr == 0x80112233u && p.guards[0].equals == 2u, "guard.0 fields");
    VR_CHECK(&fails, p.n_guards == 1, "n_guards = 1");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "guard.2.addr", "0x80445566") == 0, "guard.2.addr applies");
    VR_CHECK(&fails, p.guards[2].addr == 0x80445566u, "guard.2 addr stored");
    VR_CHECK(&fails, p.n_guards == 3, "n_guards grows to 3");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "silence.0.addr", "0x80AABBCC") == 0, "silence.0.addr applies");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "silence.0.patch", "0x00000000") == 0, "silence.0.patch applies");
    VR_CHECK(&fails, p.silence[0].addr == 0x80AABBCCu && p.silence[0].patch == 0u, "silence.0 fields");
    VR_CHECK(&fails, p.n_silence == 1, "n_silence = 1");

    memset(&p, 0, sizeof(p));
    p.separation = 7.0f;
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "stereo.sep", "notafloat") != 0, "bad float -> nonzero");
    VR_CHECK(&fails, p.separation == 7.0f, "bad float leaves field unchanged");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "stereo.sep", "0.5xyz") != 0, "trailing garbage -> nonzero");
    VR_CHECK(&fails, p.separation == 7.0f, "trailing garbage leaves field unchanged");

    p.cam_base = 0xDEADBEEFu;
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.base", "xyz") != 0, "bad hex -> nonzero");
    VR_CHECK(&fails, p.cam_base == 0xDEADBEEFu, "bad hex leaves field unchanged");

    p.headlook_mode = VR_HL_MATRIX;
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mode", "bogus") != 0, "bad enum -> nonzero");
    VR_CHECK(&fails, p.headlook_mode == VR_HL_MATRIX, "bad enum leaves field unchanged");

    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.yaw.off", "notahex") != 0, "bad op value -> nonzero");
    VR_CHECK(&fails, p.n_ops == 0, "bad op value creates no op");

    memset(&p, 0, sizeof(p));
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "does.not.exist", "1.0") == 0, "unknown key -> 0");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "cam.wobble.off", "0x4") == 0, "unknown cam axis -> 0");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "guard.99.addr", "0x80") == 0, "out-of-range guard index -> 0");
    VR_CHECK(&fails, p.n_ops == 0 && p.n_guards == 0, "unknown keys mutate nothing");

    memset(&p, 0, sizeof(p));
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "genre", "first-person shooter") == 0 &&
                     strcmp(p.genre, "first-person shooter") == 0, "genre applies");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "calibration", "measured") == 0 &&
                     p.calibration == VR_CAL_MEASURED, "calibration=measured");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "separation", "0.02") == 0 &&
                     approx(p.separation, 0.02f) && (p.present & VR_F_SEPARATION), "separation + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "convergence", "0.15") == 0 &&
                     approx(p.convergence, 0.15f) && (p.present & VR_F_CONVERGENCE), "convergence + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headLook", "true") == 0 &&
                     p.head_look == 1 && (p.present & VR_F_HEAD_LOOK), "headLook=true + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headLook", "off") == 0 && p.head_look == 0, "headLook=off");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "HEADLOOK", "1") == 0 && p.head_look == 1, "headLook key case-insensitive");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headLookSensitivity", "0.7") == 0 &&
                     approx(p.head_look_sensitivity, 0.7f) && (p.present & VR_F_HEAD_LOOK_SENSITIVITY),
                     "headLookSensitivity + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "screenDistance", "1.8") == 0 &&
                     approx(p.screen_distance, 1.8f) && (p.present & VR_F_SCREEN_DISTANCE), "screenDistance + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "screenVerticalOffset", "-0.1") == 0 &&
                     approx(p.screen_vertical_offset, -0.1f) && (p.present & VR_F_SCREEN_VERTICAL_OFFSET),
                     "screenVerticalOffset + bit");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headLook", "maybe") != 0 && p.head_look == 1, "bad bool -> nonzero, unchanged");
    VR_CHECK(&fails, vr_profile_apply_kv(&p, "headlook.mechanism", "memwrite") == 0 &&
                     p.headlook_mode == VR_HL_MEMWRITE, "headlook.mechanism -> mode");

    {
        const vr_settings def = { 0.02f, 8.0f, 0, 1.0f, 2.0f, 1.4f, 0.0f, 0.0f };
        vr_settings global, out;
        vr_profile prof, ovr;

        global = def;
        vr_profile_resolve(&def, NULL, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.separation, 0.02f) && approx(out.convergence, 8.0f) &&
                         out.head_look == 0, "resolve: empty => defaults");

        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_CONVERGENCE; prof.convergence = 0.15f;
        global = def;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.convergence, 0.15f), "resolve: profile fills untouched depth knob");

        global = def; global.convergence = 10.0f;   /* user set it (differs from default) */
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.convergence, 10.0f), "resolve: deliberate global beats profile depth");

        /* (d) TRAIT authoritative-when-specified — THE critical case:
        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_HEAD_LOOK; prof.head_look = 0;
        global = def; global.head_look = 1;          /* user enabled head-look globally */
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, out.head_look == 0, "resolve: profile headLook=off forces off over global on");

        prof.head_look = 1; global.head_look = 0;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, out.head_look == 1, "resolve: profile headLook=on forces on over global off");

        memset(&ovr, 0, sizeof(ovr));
        ovr.present = VR_F_HEAD_LOOK; ovr.head_look = 0;
        vr_profile_resolve(&def, &prof, &global, &ovr, &out);   /* prof on, override off */
        VR_CHECK(&fails, out.head_look == 0, "resolve: per-game override beats profile trait");

        /* (g) Screen-geometry FRAMING trait: profile value overrides even a deliberate
        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_SCREEN_DISTANCE; prof.screen_distance = 1.5f;
        global = def; global.screen_distance = 3.0f;   /* user tuned it */
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.screen_distance, 1.5f), "resolve: profile screenDistance overrides global (framing = game fact)");

        /* (g2) screenVerticalOffset is USER-OWNED (body ergonomics), NOT a trait:
        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_SCREEN_VERTICAL_OFFSET; prof.screen_vertical_offset = 0.1f;
        global = def; global.screen_vertical_offset = -0.2f;   /* seated user's personal offset */
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.screen_vertical_offset, -0.2f),
                 "resolve: user's voff survives a profile that sets one (voff = user fact)");

        global = def;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.screen_vertical_offset, 0.1f),
                 "resolve: profile seeds voff when the global is untouched");

        memset(&ovr, 0, sizeof(ovr));
        ovr.present = VR_F_SCREEN_VERTICAL_OFFSET; ovr.screen_vertical_offset = -0.5f;
        global = def; global.screen_vertical_offset = -0.2f;
        vr_profile_resolve(&def, &prof, &global, &ovr, &out);
        VR_CHECK(&fails, approx(out.screen_vertical_offset, -0.5f),
                 "resolve: per-game override wins voff over global and profile");

        /* (h) Clamps + comfort ceiling. sep clamps to [0,0.1]; with normalized conv the
        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_SEPARATION | VR_F_CONVERGENCE;
        prof.separation = 0.5f;      /* over-range */
        prof.convergence = 0.0f;     /* normalized: (1-conv)=1 => ceiling caps sep at 0.031 */
        global = def;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.separation, 0.031f), "resolve: comfort ceiling caps separation");

        prof.separation = 0.02f; prof.convergence = 12.0f;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.separation, 0.02f) && approx(out.convergence, 12.0f),
                 "resolve: raw clip-W convergence skips the normalized ceiling");

        /* the SAME raw clip-W path must carry a real MEASURED calibration through
        prof.separation = 0.02f; prof.convergence = 21000.0f;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.separation, 0.02f) && approx(out.convergence, 21000.0f),
                 "resolve: measured raw clip-W convergence (SM64 21000) survives resolution");

        memset(&prof, 0, sizeof(prof));
        prof.present = VR_F_SCREEN_ARC; prof.screen_arc = 400.0f;
        global = def;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.screen_arc, 360.0f), "resolve: screen_arc clamps 400 -> 360");
        prof.screen_arc = -5.0f;
        vr_profile_resolve(&def, &prof, &global, NULL, &out);
        VR_CHECK(&fails, approx(out.screen_arc, 0.0f), "resolve: screen_arc clamps -5 -> 0");
    }

    {
        vr_profile p2;
        memset(&p2, 0, sizeof p2);
        VR_CHECK(&fails, vr_profile_apply_kv(&p2, "cam.yaw.off",   "0xFE8") == 0 &&
                         vr_profile_apply_kv(&p2, "cam.yaw.scale", "-6.0") == 0 &&
                         vr_profile_apply_kv(&p2, "cam2.yaw.off",  "0x148") == 0 &&
                         vr_profile_apply_kv(&p2, "cam2.yaw.mode", "delta") == 0 &&
                         vr_profile_apply_kv(&p2, "cam2.yaw.scale","-1.0") == 0,
                 "cam2 keys parse");
        VR_CHECK(&fails, p2.n_ops == 2, "cam + cam2 on one axis = two ops");
        VR_CHECK(&fails, p2.ops[0].off == 0xFE8 && p2.ops[1].off == 0x148,
                 "slot 0 = cam.*, slot 1 = cam2.*");
        VR_CHECK(&fails, p2.ops[1].mode == VR_MODE_DELTA && p2.ops[0].scale == -6.0f,
                 "fields land on the right slots");
    }

    printf("%d check(s) failed\n", fails);
    return fails;
}
