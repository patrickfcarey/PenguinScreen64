/* mupen64plus-VR — headless unit-test entry points (packet S1 contract).
 */
#ifndef VR_TESTS_H
#define VR_TESTS_H

#include <stdio.h>

#define VR_CHECK(fails, cond, msg) do { \
        if (!(cond)) { (*(fails))++; printf("  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); } \
    } while (0)

int test_pose(void);     /* tests/vr/test_pose.c    — S5  */
int test_profile(void);  /* tests/vr/test_profile.c — S8  */
int test_camera(void);   /* tests/vr/test_camera.c  — S10 */
int test_mips(void);     /* tests/vr/test_mips.c    — S11 */

#endif /* VR_TESTS_H */
