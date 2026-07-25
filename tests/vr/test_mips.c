/* mupen64plus-VR — MIPS-III assembler unit tests (packet S11). Owns this file.
#include "vr_tests.h"
#include "vr/vr_mips.h"

int test_mips(void)
{
    int fails = 0;

    VR_CHECK(&fails, vr_mips_selftest() == 1, "vr_mips_selftest() golden table all-pass");

    /* (2) Independent hand-derived encodings — bit math worked out from the manual.
    VR_CHECK(&fails, vr_mips_lui(8, 0x8012) == 0x3C088012u, "LUI $t0,0x8012 == 0x3C088012");

    /* --- ORI $t1, $t0, 0x1234 ----------------------------------------------------
    VR_CHECK(&fails, vr_mips_ori(9, 8, 0x1234) == 0x35091234u, "ORI $t1,$t0,0x1234 == 0x35091234");

    /* --- LWC1 $f12, 0x10($a0) ----------------------------------------------------
    VR_CHECK(&fails, vr_mips_lwc1(12, 4, 0x10) == 0xC48C0010u, "LWC1 $f12,0x10($a0) == 0xC48C0010");

    /* --- %hi/%lo sign-adjust boundary at low == 0x8000 --------------------------
    {
        uint16_t hi, lo;
        vr_mips_hilo(0x80128000u, &hi, &lo);
        VR_CHECK(&fails, hi == 0x8013u && lo == 0x8000u, "hilo(0x80128000) -> hi=0x8013 lo=0x8000 (carry)");
        VR_CHECK(&fails, ((uint32_t)hi << 16) + (uint32_t)(int16_t)lo == 0x80128000u,
                 "hilo(0x80128000) reconstructs addr");

        vr_mips_hilo(0x80127FFFu, &hi, &lo);
        VR_CHECK(&fails, hi == 0x8012u && lo == 0x7FFFu, "hilo(0x80127FFF) -> hi=0x8012 lo=0x7FFF (no carry)");
    }

    printf("%d check(s) failed\n", fails);
    return fails;
}
