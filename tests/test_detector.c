/* Dedektör profilleri ve senkron el sıkışma testleri */
#include "check.h"
#include "hal_sim.h"
#include "detector.h"
#include "error_codes.h"

static exposure_sm_t sm;
static sim_t s;

/* Basit senkron dedektör modeli */
typedef struct {
    uint32_t ready_after_ms;    /* hazırlık isteğinden sonra hazır */
    uint32_t enable_after_ms;   /* ışınlama isteğinden sonra izin */
    uint32_t window_ms;         /* izin penceresi (0 = sınırsız) */
    bool never_enable;
    uint32_t prep_ms, req_ms, open_ms;
} det_model_t;

static void run(det_model_t *d, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        d->prep_ms = s.last_out.det_prep ? d->prep_ms + 1 : 0;
        d->req_ms = s.last_out.det_exp_req ? d->req_ms + 1 : 0;
        s.detector_ready = d->prep_ms >= d->ready_after_ms;
        bool en = !d->never_enable && d->req_ms >= d->enable_after_ms;
        if (en) d->open_ms++; else d->open_ms = 0;
        if (d->window_ms && d->open_ms > d->window_ms) en = false;
        s.detector_enable = en;
        sim_run(&s, &sm, 1);
    }
}

static void setup_profile(uint32_t idx)
{
    sm_config_t cfg = { .hs_dual_channel = true, .pedal_dual_channel = true, .line_readback = true };
    det_apply_config(det_builtin(idx), &cfg);
    sm_init(&sm, &cfg);
    sim_init(&s);
    s.detector_ready = false;
    sim_run(&s, &sm, 10);
}

static void set_rad(float kv, float ma, uint32_t ms)
{
    exposure_params_t p = { kv, ma, ms, FOCUS_LARGE, MODE_RADIOGRAPHY };
    sm_set_params(&sm, &p);
}

TEST(hazir_profiller_gecerli)
{
    for (uint32_t i = 0; i < det_builtin_count(); i++) {
        CHECK(det_profile_valid(det_builtin(i)), det_builtin(i)->name);
    }
    det_profile_t bad = *det_builtin(4);
    bad.in_func[2] = DET_IN_ENABLE;              /* ayni islev iki hatta */
    CHECK(!det_profile_valid(&bad), "ayni giris islevi iki hatta reddedilir");
    bad = *det_builtin(3);
    bad.in_func[0] = DET_IN_NONE;                /* izin girisi yok */
    CHECK(!det_profile_valid(&bad), "senkronda izin girisi zorunlu");
}

TEST(polarite_eslemesi)
{
    det_profile_t p = *det_builtin(4);
    p.in_active_low[1] = true;                   /* izin girisi ters */
    p.out_active_open[1] = true;                 /* istek cikisi ters */
    bool raw[3] = { true, false, false };
    sm_inputs_t in = {0};
    det_map_inputs(&p, raw, false, &in);
    CHECK(in.detector_ready && in.detector_enable && !in.detector_error, "ters polarite: gerilimsiz = aktif");
    sm_outputs_t o = {0};
    o.det_exp_req = true;
    bool ro[3];
    det_map_outputs(&p, &o, ro);
    CHECK(ro[1] == false, "aktif istek, ters cikis: kontak acik");
    o.det_exp_req = false;
    det_map_outputs(&p, &o, ro);
    CHECK(ro[1] == true, "pasif istek, ters cikis: kontak kapali");
}

TEST(kapali_profil_cr)
{
    setup_profile(0);
    det_model_t d = { 0 };
    set_rad(60, 10, 100);
    s.hs1 = true;
    s.detector_ready = false;            /* CR: dedektor yok */
    sim_run(&s, &sm, 4200);
    CHECK(sm.state == ST_READY, "CR modunda dedektor beklenmez");
    s.hs2 = true;
    sim_run(&s, &sm, 300);
    CHECK(sm.last_record.reason == END_TIME_DONE, "normal cekim");
    (void)d;
}

TEST(aed_pc_hazir)
{
    setup_profile(2);
    CHECK(sm.cfg.detector_ready_required, "PZ 3025ZF profili DR hazir bilgisini bekler");
    sm_inputs_t in = {0};
    bool raw[3] = {0};
    det_map_inputs(det_builtin(2), raw, true, &in);
    CHECK(in.detector_ready, "DR hazir -> hazir");
    det_map_inputs(det_builtin(2), raw, false, &in);
    CHECK(!in.detector_ready, "DR hazir degil -> bekle");
}

TEST(senkron_normal_cekim)
{
    setup_profile(4);
    det_model_t d = { .ready_after_ms = 300, .enable_after_ms = 30 };
    set_rad(70, 20, 100);
    s.hs1 = true;
    run(&d, 4200);
    CHECK(sm.state == ST_READY, "flaman + dedektor hazir -> HAZIR");
    s.hs2 = true;
    run(&d, 20);
    CHECK(sm.state == ST_READY && s.last_out.det_exp_req && s.xray_on_ms == 0,
          "istek gonderildi, izin beklenirken isinlama yok");
    run(&d, 400);
    CHECK(sm.last_record.reason == END_TIME_DONE, "izin geldi, normal cekim");
    CHECK(s.xray_on_ms >= 99 && s.xray_on_ms <= 101, "100 ms isinlama");
}

TEST(senkron_izin_gelmezse)
{
    setup_profile(4);
    det_model_t d = { .ready_after_ms = 300, .never_enable = true };
    set_rad(70, 20, 100);
    s.hs1 = true;
    run(&d, 4200);
    s.hs2 = true;
    run(&d, 1200);
    CHECK(sm.state == ST_LOCKED && sm.last_reason == END_DETECTOR_TIMEOUT, "izin zaman asimi -> E501");
    CHECK(s.xray_on_ms == 0, "isinlama yok");
}

TEST(senkron_pencere_erken_kapanir)
{
    setup_profile(4);
    det_model_t d = { .ready_after_ms = 300, .enable_after_ms = 30, .window_ms = 50 };
    set_rad(70, 20, 200);                        /* 200 ms isteniyor, pencere 50 ms */
    s.hs1 = true;
    run(&d, 4200);
    s.hs2 = true;
    run(&d, 400);
    CHECK(sm.state == ST_FAULT && sm.last_reason == END_DETECTOR_WINDOW, "pencere kapandi -> E502");
    CHECK(s.xray_on_ms < 60, "isinlama pencereyle birlikte kesildi");
    CHECK(fe_error_for(END_DETECTOR_WINDOW, TUBE_OK)->code == 502, "kod E502");
}

TEST(senkron_dedektor_hatasi)
{
    setup_profile(4);
    det_model_t d = { .ready_after_ms = 300, .enable_after_ms = 30 };
    set_rad(70, 20, 500);
    s.hs1 = true;
    run(&d, 4200);
    s.hs2 = true;
    run(&d, 100);
    s.detector_error = true;
    run(&d, 2);
    CHECK(sm.state == ST_FAULT && sm.last_record.reason == END_DETECTOR_ERROR, "dedektor hatasi -> E503");
    CHECK(!s.last_out.xray_on, "isinlama kesildi");
}

TEST(senkron_hazir_dusunce_lamba_soner)
{
    setup_profile(4);
    det_model_t d = { .ready_after_ms = 300, .enable_after_ms = 30 };
    s.hs1 = true;
    run(&d, 4200);
    CHECK(s.last_out.ready_indicator, "hazir");
    d.ready_after_ms = 100000;                   /* dedektor hazir sinyalini birakti */
    d.prep_ms = 0;
    run(&d, 5);
    CHECK(!s.last_out.ready_indicator && sm.state == ST_PREP, "hazir lambasi sondu");
}

int main(void)
{
    printf("Dedektor profili testleri\n");
    RUN(hazir_profiller_gecerli);
    RUN(polarite_eslemesi);
    RUN(kapali_profil_cr);
    RUN(aed_pc_hazir);
    RUN(senkron_normal_cekim);
    RUN(senkron_izin_gelmezse);
    RUN(senkron_pencere_erken_kapanir);
    RUN(senkron_dedektor_hatasi);
    RUN(senkron_hazir_dusunce_lamba_soner);
    return REPORT();
}
