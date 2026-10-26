/* Dirençle denetlenen el butonu hattı testleri */
#include "check.h"
#include "hs_line.h"
#include "exposure_sm.h"
#include "hal_sim.h"
#include <string.h>

static uint16_t code(float ratio) { return (uint16_t)(ratio * 4095.0f + 0.5f); }

TEST(nominal_durumlar)
{
    const float rpu = HS_LINE_RPU_OHM, rs = HS_LINE_RS_OHM, rp = HS_LINE_RP_OHM;
    CHECK(hs_line_classify(code(hs_line_ratio(rs, rpu))) == HS_LINE_PRESSED, "basili");
    CHECK(hs_line_classify(code(hs_line_ratio(rs + rp, rpu))) == HS_LINE_RELEASED, "birakilmis");
    CHECK(hs_line_classify(0) == HS_LINE_SHORT, "kisa devre");
    CHECK(hs_line_classify(4095) == HS_LINE_OPEN, "kopuk");
    CHECK(hs_line_classify(code(0.48f)) == HS_LINE_INVALID, "pencere arasi gecersiz");
}

/* %5'lik dirençlerin tüm köşe kombinasyonları + 0..10 ohm kablo */
TEST(tolerans_koseleri)
{
    const float t[2] = {0.95f, 1.05f};
    int bad = 0;
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) for (int c = 0; c < 2; c++)
    for (int w = 0; w <= 10; w += 10) {
        const float rpu = HS_LINE_RPU_OHM * t[a];
        const float rs = HS_LINE_RS_OHM * t[b] + (float)w;
        const float rp = HS_LINE_RP_OHM * t[c];
        if (hs_line_classify(code(hs_line_ratio(rs, rpu))) != HS_LINE_PRESSED) bad++;
        if (hs_line_classify(code(hs_line_ratio(rs + rp, rpu))) != HS_LINE_RELEASED) bad++;
        if (hs_line_classify(code(hs_line_ratio((float)w, rpu))) != HS_LINE_SHORT) bad++;
    }
    CHECK(bad == 0, "%5 direnc ve kablo direnciyle tum koseler dogru siniflanir");
}

TEST(eslemeler_guvenli_taraf)
{
    bool no, nc;
    hs_line_map(HS_LINE_PRESSED, &no, &nc);  CHECK(no && !nc, "basili -> NO=1 NC=0");
    hs_line_map(HS_LINE_RELEASED, &no, &nc); CHECK(!no && nc, "birakilmis -> NO=0 NC=1");
    hs_line_state_t f[3] = {HS_LINE_SHORT, HS_LINE_OPEN, HS_LINE_INVALID};
    for (int i = 0; i < 3; i++) {
        hs_line_map(f[i], &no, &nc);
        CHECK(no == nc, "ariza -> NO/NC tutarsiz (E402)");
        CHECK(!(no && !nc), "ariza hicbir zaman 'basili' sayilmaz");
    }
}

/* Uçtan uca: simülatör butonu ADC kodlarıyla okur */
static uint16_t PR, RL;

static void setup(exposure_sm_t *sm, sim_t *s)
{
    sm_config_t c = { .hs_dual_channel = true, .pedal_dual_channel = true,
                      .line_readback = true };
    sm_init(sm, &c);
    sim_init(s);
    s->use_hs_adc = true;
    PR = code(hs_line_ratio(HS_LINE_RS_OHM, HS_LINE_RPU_OHM));
    RL = code(hs_line_ratio(HS_LINE_RS_OHM + HS_LINE_RP_OHM, HS_LINE_RPU_OHM));
    s->hs1_adc = RL; s->hs2_adc = RL;
    sim_run(s, sm, 10);
    exposure_params_t p = { 70, 20, 500, FOCUS_LARGE, MODE_RADIOGRAPHY };
    sm_set_params(sm, &p);
}

TEST(normal_cekim_adc_ile)
{
    exposure_sm_t sm; sim_t s; setup(&sm, &s);
    s.hs1_adc = PR; sim_run(&s, &sm, 4100);
    CHECK(sm.state == ST_READY, "1. kademe -> HAZIR");
    s.hs2_adc = PR; sim_run(&s, &sm, 600);
    CHECK(sm.state == ST_LOCKED && sm.last_record.reason == END_TIME_DONE, "sure doldu, normal bitis");
    s.hs1_adc = s.hs2_adc = RL; sim_run(&s, &sm, 50);
    CHECK(sm.state == ST_IDLE, "birakinca BEKLE");
}

TEST(isinlamada_kablo_kisa_devresi)
{
    exposure_sm_t sm; sim_t s; setup(&sm, &s);
    s.hs1_adc = PR; sim_run(&s, &sm, 4100);
    s.hs2_adc = PR; sim_run(&s, &sm, 100);
    CHECK(sm.state == ST_EXPOSING, "ISINLAMA");
    s.hs2_adc = 0; sim_run(&s, &sm, 1);
    CHECK(!s.last_out.xray_on, "kisa devre ayni adimda X-RAY ON'u keser");
    sim_run(&s, &sm, 30);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "kablo arizasi (E402)");
}

TEST(beklemede_kablo_kisa_devresi_cekim_yapmaz)
{
    exposure_sm_t sm; sim_t s; setup(&sm, &s);
    s.hs1_adc = 0; s.hs2_adc = 0;     /* iki kademe kablosu birden kısa devre */
    sim_run(&s, &sm, 5000);
    CHECK(s.xray_on_ms == 0, "kisa devre hicbir zaman cekim baslatmaz");
    CHECK(s.filament_heat_ms == 0, "flaman bile isitilmaz");
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "E402");
}

TEST(kopuk_kablo)
{
    exposure_sm_t sm; sim_t s; setup(&sm, &s);
    s.hs1_adc = PR; sim_run(&s, &sm, 4100);
    s.hs2_adc = PR; sim_run(&s, &sm, 100);
    s.hs1_adc = 4095; sim_run(&s, &sm, 1);
    CHECK(!s.last_out.xray_on, "1. kademe kablosu kopunca isinlama durur");
    sim_run(&s, &sm, 30);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_INPUT_CHANNEL, "E402");
}

int main(void)
{
    RUN(nominal_durumlar);
    RUN(tolerans_koseleri);
    RUN(eslemeler_guvenli_taraf);
    RUN(normal_cekim_adc_ile);
    RUN(isinlamada_kablo_kisa_devresi);
    RUN(beklemede_kablo_kisa_devresi_cekim_yapmaz);
    RUN(kopuk_kablo);
    return REPORT();
}
