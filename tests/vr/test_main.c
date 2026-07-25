/* mupen64plus-VR — headless VR unit-test runner (packet S4).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vr_tests.h"

static const struct { const char* name; int (*fn)(void); } k_suites[] = {
    { "pose",    test_pose    },  /* S5  */
    { "profile", test_profile },  /* S8  */
    { "camera",  test_camera  },  /* S10 */
    { "mips",    test_mips    },  /* S11 */
};
#define N_SUITES ((int)(sizeof(k_suites) / sizeof(k_suites[0])))

/* Run one suite with its stdout captured, so we can reprint its detail under a
static int run_suite(const char* name, int (*fn)(void), int* skipped)
{
    FILE* cap = tmpfile();
    int   saved_fd = -1;
    int   capturing = 0;


    if (cap != NULL) {
        fflush(stdout);
        saved_fd = dup(1);
        if (saved_fd >= 0 && dup2(fileno(cap), 1) >= 0) {
            capturing = 1;
        } else if (saved_fd >= 0) {
            close(saved_fd);
            saved_fd = -1;
        }
    }

    int fails = fn();   /* suite prints its detail to the (captured) fd 1 */

    char* text = NULL;
    if (capturing) {
        fflush(stdout);
        dup2(saved_fd, 1);          /* restore the real stdout */
        close(saved_fd);

        fseek(cap, 0, SEEK_END);
        long n = ftell(cap);
        if (n < 0) n = 0;
        rewind(cap);
        text = (char*)malloc((size_t)n + 1);
        if (text != NULL) {
            size_t got = fread(text, 1, (size_t)n, cap);
            text[got] = '\0';
        }
    }
    if (cap != NULL)
        fclose(cap);

    if (fails == 0 && text != NULL && strstr(text, "SKIP") != NULL)

    printf("-- %s --\n", name);
    if (text != NULL && text[0] != '\0')
        fputs(text, stdout);
    printf("[%s] %s", fails ? "FAIL" : (*skipped ? "SKIP" : "PASS"), name);
    if (fails)
        printf(" (%d failed check%s)", fails, fails == 1 ? "" : "s");
    printf("\n\n");

    free(text);
    return fails;
}

int main(void)
{
    printf("== mupen64plus-VR unit tests ==\n\n");

    int total_fails = 0, n_pass = 0, n_fail = 0, n_skip = 0;

    for (int i = 0; i < N_SUITES; i++) {
        int skipped = 0;
        int fails = run_suite(k_suites[i].name, k_suites[i].fn, &skipped);
        total_fails += fails;
        if (fails)          n_fail++;
        else if (skipped)   n_skip++;
        else                n_pass++;
    }

    printf("== summary: %d suite%s | %d passed, %d failed, %d skipped | %d failed check%s ==\n",
           N_SUITES, N_SUITES == 1 ? "" : "s",
           n_pass, n_fail, n_skip,
           total_fails, total_fails == 1 ? "" : "s");
    printf("== RESULT: %s ==\n", n_fail ? "FAIL" : "PASS");

    return n_fail ? 1 : 0;
}
