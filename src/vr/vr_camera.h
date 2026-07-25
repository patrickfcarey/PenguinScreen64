/* mupen64plus-VR — Tier-3B camera engine (packet S1 contract; S10 scaffold, S18 engine).
 */
#ifndef M64P_VR_CAMERA_H
#define M64P_VR_CAMERA_H

#include <stdint.h>
#include "api/m64p_types.h"
#include "vr_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

struct r4300_core;

/* Arm/capture state carried across VIs (auto-recenter semantics): when the guards
typedef struct {
    int   armed;
    float base_val[VR_MAX_OPS];     /* compose: game value @arm; anchored: LIVE baseline */
    float head_ref[VR_MAX_OPS];     /* head axis captured at arm (output units)   */
    float last_written[VR_MAX_OPS]; /* anchored: our previous write — the game-vs-us
                                       attribution reference (post-wrap/clamp)     */
} vr_camera_state;

void vr_camera_state_reset(vr_camera_state* st);

/* PURE/testable. Evaluate `prof` against `pose` and write into `dram`
int vr_camera_eval(uint32_t* dram, uint32_t dram_size_bytes,
                   const vr_profile* prof, const m64p_vr_pose* pose,
                   float sensitivity, vr_camera_state* st);

void vr_camera_request_recenter(void);

#ifndef VR_TESTING
/* Production. Called once per VI from new_vi() (main.c) after gs_apply_cheats().
void vr_camera_apply(struct r4300_core* r4300);
#endif

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_CAMERA_H */
