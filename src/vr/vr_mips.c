/* mupen64plus-VR — MIPS-III code-cave assembler (packet S11). Ports the emitters +
 */
#include "vr_mips.h"
#include <stdio.h>

#define R(x)   ((uint32_t)((x) & 31))
#define I16(x) ((uint32_t)(x) & 0xFFFFu)

uint32_t vr_mips_lui  (int rt, uint16_t imm)          { return 0x3C000000u |                (R(rt) << 16) | I16(imm); }
uint32_t vr_mips_ori  (int rt, int rs, uint16_t imm)  { return 0x34000000u | (R(rs) << 21) | (R(rt) << 16) | I16(imm); }
uint32_t vr_mips_addiu(int rt, int rs, int16_t imm)   { return 0x24000000u | (R(rs) << 21) | (R(rt) << 16) | I16(imm); }
uint32_t vr_mips_lw   (int rt, int base, int16_t off) { return 0x8C000000u | (R(base) << 21) | (R(rt) << 16) | I16(off); }
uint32_t vr_mips_sw   (int rt, int base, int16_t off) { return 0xAC000000u | (R(base) << 21) | (R(rt) << 16) | I16(off); }
uint32_t vr_mips_lwc1 (int ft, int base, int16_t off) { return 0xC4000000u | (R(base) << 21) | (R(ft) << 16) | I16(off); }
uint32_t vr_mips_swc1 (int ft, int base, int16_t off) { return 0xE4000000u | (R(base) << 21) | (R(ft) << 16) | I16(off); }

uint32_t vr_mips_add_s(int fd, int fs, int ft) { return 0x46000000u | (R(ft) << 16) | (R(fs) << 11) | (R(fd) << 6); }

uint32_t vr_mips_j  (uint32_t target) { return 0x08000000u | ((target >> 2) & 0x03FFFFFFu); }
uint32_t vr_mips_jal(uint32_t target) { return 0x0C000000u | ((target >> 2) & 0x03FFFFFFu); }
uint32_t vr_mips_jr (int rs)          { return (R(rs) << 21) | 0x00000008u; }
uint32_t vr_mips_nop(void)            { return VR_MIPS_NOP; }

void vr_mips_hilo(uint32_t addr, uint16_t *hi, uint16_t *lo)
{
}

int vr_mips_selftest(void)
{
    int fail = 0;
#define EQ(got, want, name) do {                                                          \
        uint32_t g_ = (uint32_t)(got), w_ = (uint32_t)(want);                             \
        if (g_ != w_) { fail++;                                                           \
            printf("(vr_mips) selftest FAIL: %s got 0x%08X want 0x%08X\n", (name), g_, w_); } \
    } while (0)

    /* ── PCSX2's golden table (CameraDriver.cpp:363-392), encodings bit-identical ──
    EQ(vr_mips_add_s(13, 31, 13), 0x460DFB40u, "add.s f13,f31,f13 (community-verified)");
    EQ(vr_mips_j    (0x0036CC40u), 0x080DB310u, "j 0x0036CC40");
    EQ(vr_mips_jal  (0x000F1100u), 0x0C03C440u, "jal 0x000F1100");
    EQ(vr_mips_lui  (1, 0x000Fu),  0x3C01000Fu, "lui $1,0x000F");

    /* ── new-emitter goldens (each instruction the packet lists, representative
    EQ(vr_mips_ori  (9, 8, 0x3456u), 0x35093456u, "ori   $t1,$t0,0x3456");
    EQ(vr_mips_addiu(29, 29, -16),   0x27BDFFF0u, "addiu $sp,$sp,-16");
    EQ(vr_mips_lw   (8, 29, 0x20),   0x8FA80020u, "lw    $t0,0x20($sp)");
    EQ(vr_mips_sw   (31, 29, 0x1C),  0xAFBF001Cu, "sw    $ra,0x1C($sp)");
    EQ(vr_mips_lwc1 (12, 4, 0x10),   0xC48C0010u, "lwc1  $f12,0x10($a0)");
    EQ(vr_mips_swc1 (12, 4, 0x10),   0xE48C0010u, "swc1  $f12,0x10($a0)");
    EQ(vr_mips_jr   (31),            0x03E00008u, "jr    $ra");
    EQ(vr_mips_nop  (),              0x00000000u, "nop");

    /* ── %hi/%lo sign-adjust: lo with bit 15 SET must carry +1 into hi ──────────
    {
        uint16_t hi, lo;
        vr_mips_hilo(0x0010A800u, &hi, &lo);
        EQ(hi, 0x0011u, "hilo carry: hi==0x0011");
        EQ(lo, 0xA800u, "hilo carry: lo==0xA800");
        EQ(((uint32_t)hi << 16) + (uint32_t)(int16_t)lo, 0x0010A800u, "hilo reconstructs addr");
    }
    {
        uint16_t hi, lo;
        vr_mips_hilo(0x00107FFFu, &hi, &lo);
        EQ(hi, 0x0010u, "hilo no-carry: hi==0x0010");
        EQ(lo, 0x7FFFu, "hilo no-carry: lo==0x7FFF");
    }

    /* ── Full GT4 cave assembled through the real emitters + splitter (PCSX2
    {
        uint16_t hi, lo;
        vr_mips_hilo(0x000F1120u, &hi, &lo);            /* scratch_address */
        EQ(vr_mips_lui (1, hi),               0x3C01000Fu, "GT4 cave[0] lui  $1,0x000F");
        EQ(vr_mips_lwc1(1, 1, (int16_t)lo),   0xC4211120u, "GT4 cave[1] lwc1 $f1,0x1120($1)");
        EQ(vr_mips_add_s(13, 13, 1),          0x46016B40u, "GT4 cave[2] add.s f13,f13,f1");
        EQ(vr_mips_j   (0x0036CC40u),         0x080DB310u, "GT4 cave[3] j 0x0036CC40");
        EQ(vr_mips_nop (),                    0x00000000u, "GT4 cave[4] nop");
        EQ(vr_mips_jal (0x000F1100u),         0x0C03C440u, "GT4 trampoline jal 0x000F1100");
    }

#undef EQ
    return fail == 0 ? 1 : 0;
}
