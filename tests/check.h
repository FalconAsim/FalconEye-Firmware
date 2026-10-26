/* Küçük test yardımcısı: harici kütüphane gerektirmez */
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  BASARISIZ: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

#define TEST(name) static void name(void)
#define RUN(name) do { printf("- %s\n", #name); name(); } while (0)

#define REPORT() (printf("\n%d kontrol gecti, %d basarisiz\n", g_pass, g_fail), g_fail ? 1 : 0)

#endif
