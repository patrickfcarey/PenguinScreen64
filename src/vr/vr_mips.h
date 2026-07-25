/* mupen64plus-VR — MIPS-III code-cave assembler (packet S1 contract; S11 owns it).
 */
#ifndef M64P_VR_MIPS_H
#define M64P_VR_MIPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Emitters ─────────────────────────────────────────────────────────────────

#define VR_MIPS_NOP 0x00000000u  /* nop == sll $zero,$zero,0 */

uint32_t vr_mips_lui  (int rt, uint16_t imm);          /* lui   rt, imm            */
uint32_t vr_mips_ori  (int rt, int rs, uint16_t imm);  /* ori   rt, rs, imm        */
uint32_t vr_mips_addiu(int rt, int rs, int16_t imm);   /* addiu rt, rs, imm        */
uint32_t vr_mips_lw   (int rt, int base, int16_t off); /* lw    rt, off(base)      */
uint32_t vr_mips_sw   (int rt, int base, int16_t off); /* sw    rt, off(base)      */
uint32_t vr_mips_lwc1 (int ft, int base, int16_t off); /* lwc1  ft, off(base)      */
uint32_t vr_mips_swc1 (int ft, int base, int16_t off); /* swc1  ft, off(base)      */
uint32_t vr_mips_add_s(int fd, int fs, int ft);        /* add.s fd, fs, ft  (COP1) */
uint32_t vr_mips_j    (uint32_t target);               /* j     target             */
uint32_t vr_mips_jal  (uint32_t target);               /* jal   target             */
uint32_t vr_mips_jr   (int rs);                        /* jr    rs                 */
uint32_t vr_mips_nop  (void);                          /* nop                      */

/* %hi/%lo relocation split with the standard +1 sign-adjust. Because the low half
void vr_mips_hilo(uint32_t addr, uint16_t *hi, uint16_t *lo);

/* Run every emitter against hand-computed golden words (ports PCSX2's table at
int vr_mips_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* M64P_VR_MIPS_H */
