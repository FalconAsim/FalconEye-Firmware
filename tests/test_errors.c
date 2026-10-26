/* Hata kodu tablosu tutarlılık testleri */
#include "check.h"
#include "error_codes.h"
#include <string.h>

TEST(her_neden_icin_kod_var)
{
    for (int r = END_NONE; r < END__COUNT; r++) {
        const fe_error_t *e = fe_error_for((end_reason_t)r, TUBE_ERR_RATING);
        const int normal = (r == END_NONE || r == END_TIME_DONE || r == END_RELEASED);
        char msg[64];
        snprintf(msg, sizeof msg, "neden %d icin kod", r);
        CHECK(normal ? e == NULL : e != NULL, msg);
    }
}

TEST(her_tup_sonucu_icin_ayri_kod)
{
    uint16_t seen[32] = {0};
    for (int t = TUBE_ERR_KV_LOW; t <= TUBE_ERR_PARAM; t++) {
        const fe_error_t *e = fe_error_for(END_TUBE_REJECT, (tube_result_t)t);
        CHECK(e != NULL && e->code >= 200 && e->code < 300, "tup sonucu E2xx");
        if (e) {
            for (int k = 0; k < t; k++) CHECK(seen[k] != e->code, "tup kodlari benzersiz");
            seen[t] = e->code;
        }
    }
}

TEST(kodlar_benzersiz_ve_metinler_dolu)
{
    uint32_t n;
    const fe_error_t *t = fe_error_table(&n);
    CHECK(n >= 25, "tablo dolu");
    for (uint32_t i = 0; i < n; i++) {
        CHECK(t[i].title && strlen(t[i].title) > 3, "baslik var");
        CHECK(t[i].cause && strlen(t[i].cause) > 10, "neden var");
        CHECK(t[i].action && strlen(t[i].action) > 10, "yapilacak var");
        for (uint32_t j = i + 1; j < n; j++) CHECK(t[i].code != t[j].code, "kod benzersiz");
    }
}

TEST(servis_kodlari)
{
    CHECK(fe_error_for(END_LINE_FAULT, TUBE_OK)->severity == SEV_SERVICE, "E405 servis");
    CHECK(fe_error_for(END_BACKUP_TIMER, TUBE_OK)->severity == SEV_SERVICE, "E401 servis");
    CHECK(fe_error_for(END_EARLY_STAGE2, TUBE_OK)->severity == SEV_INFO, "E101 bilgi");
}

int main(void)
{
    printf("Hata kodu tablosu testleri\n");
    RUN(her_neden_icin_kod_var);
    RUN(her_tup_sonucu_icin_ayri_kod);
    RUN(kodlar_benzersiz_ve_metinler_dolu);
    RUN(servis_kodlari);
    return REPORT();
}
