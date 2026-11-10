/*
 * Işınlama durum makinesi - uygulama
 *
 * Akış (radyografi):
 *   BEKLE --1. kademe--> HAZIRLIK --4 s + Filaman tamam--> HAZIR
 *   HAZIR --2. kademe--> IŞINLAMA --süre doldu / bırakıldı--> KİLİT
 *   KİLİT --butonlar tamamen bırakıldı--> BEKLE
 *   Hazır olmadan 2. kademe --> KİLİT (tam bırakma gerekir)
 *
 * Floroskopi: pedal basılınca HAZIRLIK, hazır olunca pedal basılı
 * oldukça ışınlama; pedal bırakılınca veya süre dolunca biter.
 */
#include "exposure_sm.h"
#include "fe_config.h"
#include <string.h>
#include <math.h>

static void outputs_off(sm_outputs_t *o)
{
    memset(o, 0, sizeof(*o));
}

static void enter(exposure_sm_t *sm, sm_state_t s)
{
    sm->state = s;
    sm->t_state_ms = 0u;
    sm->stuck_ms = 0u;
}

/* Filaman hazır bilgisi: sinyal yoksa (filament_ready_by_timer) hazır,
 * ısınma süresiyle belirlenir */
static bool fil_ok(const exposure_sm_t *sm, const sm_inputs_t *in)
{
    return sm->cfg.filament_ready_by_timer || in->filament_ok;
}

void sm_init(exposure_sm_t *sm, const sm_config_t *cfg)
{
    memset(sm, 0, sizeof(*sm));
    sm->cfg = *cfg;
    tube_init(&sm->tube);
    if (cfg->gen_power_max_w > 0.0f) sm->tube.gen_power_max_w = cfg->gen_power_max_w;
    sm_load_boot_defaults(sm);
    enter(sm, ST_IDLE);
}

/* Açılış değerleri (Asım, 26.10.2026): radyografi modu (floroskopi değil),
 * insert çift fokusluysa KÜÇÜK fokus. Değerler küçük fokusun yük eğrisi
 * içinde kalır. Tek fokuslu insertte büyük (tek) fokus. */
void sm_load_boot_defaults(exposure_sm_t *sm)
{
    const bool small = tube_has_small_focus(&sm->tube);
    sm->params.kv = 60.0f;
    sm->params.ma = small ? 10.0f : 5.0f;
    sm->params.time_ms = 100u;
    sm->params.focus = small ? FOCUS_SMALL : FOCUS_LARGE;
    sm->params.mode = MODE_RADIOGRAPHY;
    sm->saved_rad = sm->params;
    sm->have_saved_fluoro = false;
    /* Röle bırakık = büyük fokus; küçük fokus komutu ilk adımda verilir ve
     * 50 ms oturma süresi beklenir. */
    sm->focus_cmd_small = false;
    sm->focus_settle_ms = FE_FOCUS_SETTLE_MS;
}

sm_set_result_t sm_set_params_ex(exposure_sm_t *sm, const exposure_params_t *p)
{
    if (sm->state == ST_EXPOSING) {
        return SM_SET_BUSY;   /* ışınlamada değerler donar */
    }
    if (p->mode == MODE_FLUORO && !sm_fluoro_available(sm)) {
        return SM_SET_FLUORO_DISABLED;
    }
    if (p->focus == FOCUS_LARGE && !sm_focus_available(sm, FOCUS_LARGE)) {
        return SM_SET_FOCUS_DISABLED;
    }
    if (sm->state == ST_PREP || sm->state == ST_READY) {
        /* Fokus rölesi yük altında anahtarlanmaz; mod değişimi (el butonu /
         * pedal) hazırlığın anlamını değiştirir. İkisi de hazırlıkta yasak. */
        if (p->focus != sm->params.focus || p->mode != sm->params.mode) {
            return SM_SET_FOCUS_LOCKED;
        }
        const bool changed = p->kv != sm->params.kv || p->ma != sm->params.ma ||
                             p->time_ms != sm->params.time_ms;
        sm->params = *p;
        if (changed) {
            /* Hazır lambası söner, ısınma baştan. O anda 2. kademe basılıysa
             * HAZIRLIK'taki "hazır olmadan 2. kademe" kuralı çekimi engeller. */
            enter(sm, ST_PREP);
            sm->warmup_count++;
        }
        return SM_SET_OK;
    }
    sm->params = *p;
    return SM_SET_OK;
}

bool sm_set_params(exposure_sm_t *sm, const exposure_params_t *p)
{
    return sm_set_params_ex(sm, p) == SM_SET_OK;
}

bool sm_focus_available(const exposure_sm_t *sm, focus_t focus)
{
    if (focus == FOCUS_SMALL) return tube_has_small_focus(&sm->tube);
    return !(tube_has_small_focus(&sm->tube) && sm->cfg.large_focus_off);
}

bool sm_fluoro_available(const exposure_sm_t *sm)
{
    return sm->cfg.fluoro_enabled && tube_has_fluoro(&sm->tube);
}

void sm_mode_target(const exposure_sm_t *sm, exp_mode_t mode, exposure_params_t *out)
{
    if (mode == sm->params.mode) {
        *out = sm->params;
    } else if (mode == MODE_FLUORO) {
        if (sm->have_saved_fluoro) {
            *out = sm->saved_fluoro;
        } else {
            out->kv = 50.0f;
            out->ma = 2.0f;
            out->time_ms = FE_FLUORO_TIME_MAX_MS;
            out->focus = FOCUS_SMALL;
            out->mode = MODE_FLUORO;
        }
    } else {
        *out = sm->saved_rad;
        out->mode = MODE_RADIOGRAPHY;
    }
}

sm_set_result_t sm_select_mode(exposure_sm_t *sm, exp_mode_t mode)
{
    if (mode == sm->params.mode) return SM_SET_OK;
    exposure_params_t p;
    sm_mode_target(sm, mode, &p);
    const exposure_params_t before = sm->params;
    const sm_set_result_t r = sm_set_params_ex(sm, &p);
    if (r == SM_SET_OK) {
        if (before.mode == MODE_FLUORO) {
            sm->saved_fluoro = before;
            sm->have_saved_fluoro = true;
        } else {
            sm->saved_rad = before;
        }
    }
    return r;
}

sm_set_result_t sm_toggle_mode(exposure_sm_t *sm)
{
    return sm_select_mode(sm, sm->params.mode == MODE_FLUORO ? MODE_RADIOGRAPHY : MODE_FLUORO);
}

bool sm_record_dap(exposure_sm_t *sm, float dgycm2)
{
    if (!sm->cfg.dap_meter_enabled || !sm->last_record.valid || !(dgycm2 >= 0.0f)) return false;
    sm->last_record.dap_dgycm2 = dgycm2;
    sm->last_record.dap_valid = true;
    return true;
}

bool sm_set_pc_link(exposure_sm_t *sm, bool required, bool serial)
{
    if (sm->state != ST_IDLE && sm->state != ST_LOCKED) {
        return false;
    }
    sm->cfg.pc_link_required = required;
    sm->cfg.pc_link_serial = serial;
    return true;
}

static bool is_fault_reason(end_reason_t r)
{
    return r == END_GEN_ALARM || r == END_KV_LOW || r == END_KV_HIGH || r == END_MA_TOL ||
           r == END_BACKUP_TIMER || r == END_ESTOP ||
           r == END_BUTTON_STUCK || r == END_INPUT_CHANNEL || r == END_ESTOP_CHANNEL ||
           r == END_LINE_FAULT || r == END_TIME_DEV || r == END_DETECTOR_ERROR ||
           r == END_DETECTOR_WINDOW || r == END_LINK_LOST || r == END_LINK_LOST_SERIAL ||
           r == END_FOCUS_RELAY || r == END_REF_READBACK;
}

static bool is_fluoro(const exposure_sm_t *sm)
{
    return sm->params.mode == MODE_FLUORO;
}

/* Hazırlık/hazır durumunda operatör hâlâ basılı tutuyor mu? */
static bool prep_held(const exposure_sm_t *sm, const sm_inputs_t *in)
{
    return is_fluoro(sm) ? in->pedal : in->hs_stage1;
}

static void finish_exposure(exposure_sm_t *sm, end_reason_t reason)
{
    exposure_record_t *r = &sm->last_record;
    r->set = sm->params;
    r->actual_ms = sm->t_expose_ms;
    r->mas_measured = sm->mas_acc;
    r->kv_avg = sm->n_samples ? sm->kv_sum / (float)sm->n_samples : 0.0f;
    r->ma_avg = sm->n_samples ? sm->ma_sum / (float)sm->n_samples : 0.0f;
    r->reason = reason;
    r->cut = reason == END_LINK_LOST || reason == END_LINK_LOST_SERIAL;
    r->dap_valid = false;      /* DAP metre okuması sonra gelir (sm_record_dap) */
    r->cal_kv_used = false;
    r->cal_ma_used = false;
    r->dap_dgycm2 = 0.0f;
    r->valid = true;

    if (sm->t_expose_ms > 0u) {
        tube_exposure_done(&sm->tube, sm->params.focus, sm->t_expose_ms);
    }
    if (is_fluoro(sm)) {
        sm->fluoro_cumulative_ms += sm->t_expose_ms;
        if (sm->fluoro_cumulative_ms >= FE_FLUORO_CUMULATIVE_ALERT_MS) {
            sm->fluoro_alert = true;
        }
    }

    sm->last_reason = reason;
    sm->rl5_hold_ms = SM_RL5_RELEASE_MS;
    enter(sm, is_fault_reason(reason) ? ST_FAULT : ST_LOCKED);
}

static void lock(exposure_sm_t *sm, end_reason_t reason)
{
    sm->last_reason = reason;
    enter(sm, is_fault_reason(reason) ? ST_FAULT : ST_LOCKED);
}

/* Işınlama süresi ölçümü: kV geri beslemesinin eşiğin (%75) üzerinde
 * kaldığı süre. Işınlama bittikten sonra kV eşiğin altına düşünce kapanır
 * ve ayarlanan süreyle karşılaştırılır. */
static void irr_measure(exposure_sm_t *sm, const sm_inputs_t *in, uint32_t dt)
{
    if (!sm->irr_active) return;
    if (in->kv_fb >= sm->irr_thr_kv) sm->irr_ms += dt;
    if (sm->state == ST_EXPOSING) return;

    sm->irr_after_ms += dt;
    if (in->kv_fb >= sm->irr_thr_kv && sm->irr_after_ms < 500u) return;

    sm->irr_active = false;
    sm->last_record.irr_ms = sm->irr_ms;
    if (sm->last_record.reason == END_TIME_DONE && sm->state == ST_LOCKED &&
        sm->params.mode == MODE_RADIOGRAPHY) {
        const uint32_t set = sm->last_record.set.time_ms;
        const uint32_t dev = (sm->irr_ms > set) ? sm->irr_ms - set : set - sm->irr_ms;
        const float tol = (float)set * FE_TIME_TOL_FRAC + (float)FE_TIME_TOL_MS;
        if ((float)dev > tol) {
            sm->last_reason = END_TIME_DEV;
            sm->state = ST_FAULT;    /* zamanlayıcıları sıfırlamadan */
        }
    }
}

/* Işınlama sırasında kV/mA toleransı; FE_KV_RISE_MS sonrası */
static end_reason_t check_tolerance(exposure_sm_t *sm, const sm_inputs_t *in, uint32_t dt)
{
    if (sm->t_expose_ms < FE_KV_RISE_MS) {
        return END_NONE;
    }
    const float ma_err = fabsf(in->ma_fb - sm->params.ma);
    const bool kv_low = in->kv_fb < sm->params.kv * FE_KV_LOW_FRAC;
    const bool kv_high = in->kv_fb > sm->params.kv * FE_KV_HIGH_FRAC;

    sm->tol_kv_ms = (kv_low || kv_high) ? sm->tol_kv_ms + dt : 0u;
    sm->tol_ma_ms = (ma_err > sm->params.ma * FE_MA_TOL_FRAC) ? sm->tol_ma_ms + dt : 0u;

    if (sm->tol_kv_ms >= FE_TOL_VIOLATION_MS) return kv_low ? END_KV_LOW : END_KV_HIGH;
    if (sm->tol_ma_ms >= FE_TOL_VIOLATION_MS) return END_MA_TOL;
    return END_NONE;
}

/*
 * Buton/pedal kanal denetimi.
 * El butonu: her kademe NO + NC kontak. Basılı = NO kapalı VE NC açık.
 * Pedal: iki bağımsız kanal. Basılı = A VE B.
 * Kablo kısa devresi veya kopukluğu kanalları tutarsız yapar -> hata.
 * Etkin "basılı" için iki kanal birden gerekir; biri bile "bırakıldı"
 * derse ışınlama durur.
 */
static void filter_inputs(const exposure_sm_t *sm, const sm_inputs_t *raw,
                          sm_inputs_t *eff, bool *raw_any, bool *chan_bad)
{
    *eff = *raw;
    const bool dh = sm->cfg.hs_dual_channel;
    const bool dp = sm->cfg.pedal_dual_channel;

    if (dh) {
        eff->hs_stage1 = raw->hs_stage1 && !raw->hs_stage1_nc;
        eff->hs_stage2 = raw->hs_stage2 && !raw->hs_stage2_nc;
    }
    if (dp) {
        eff->pedal = raw->pedal && raw->pedal_b;
    }

    /* Herhangi bir kanal "basılı" diyorsa tam bırakılmış sayılmaz */
    *raw_any = raw->hs_stage1 || raw->hs_stage2 || raw->pedal ||
               (dh && (!raw->hs_stage1_nc || !raw->hs_stage2_nc)) ||
               (dp && raw->pedal_b);

    *chan_bad = (dh && (raw->hs_stage1 == raw->hs_stage1_nc ||
                        raw->hs_stage2 == raw->hs_stage2_nc)) ||
                (dp && raw->pedal != raw->pedal_b);
}

void sm_step(exposure_sm_t *sm, const sm_inputs_t *raw, sm_outputs_t *out, uint32_t dt)
{
    sm_inputs_t eff;
    bool raw_any, chan_bad;
    filter_inputs(sm, raw, &eff, &raw_any, &chan_bad);
    const sm_inputs_t *in = &eff;

    outputs_off(out);
    sm->t_state_ms += dt;
    irr_measure(sm, in, dt);

    /* Açılışta basılı buton: takılı veya kısa devre */
    if (!sm->booted) {
        sm->booted = true;
        if (raw_any) {
            lock(sm, END_BUTTON_STUCK);
        }
    }

    /* Acil stop: iki kanaldan BİRİ açılsa yeter (güvenli taraf).
     * Kanallar 100 ms'den uzun farklı kalırsa kablo/kontak arızası. */
    const bool estop_pressed = !raw->estop_ok || !raw->estop_ok_b;
    eff.estop_ok = !estop_pressed;
    sm->estop_bad_ms = (raw->estop_ok != raw->estop_ok_b) ? sm->estop_bad_ms + dt : 0u;
    if (sm->estop_bad_ms >= FE_ESTOP_MISMATCH_MS && sm->state != ST_FAULT) {
        lock(sm, END_ESTOP_CHANNEL);
    }

    /* J2 hat geri okuması: bir önceki adımın komutu ile hattın gerçek durumu
     * karşılaştırılır. Komut yokken aktif hat = GND'ye kısa devre veya yapışık
     * PhotoMOS; komut varken pasif hat = kopukluk veya açık seri kontak. */
    if (sm->cfg.line_readback) {
        const bool bad = (raw->xray_line_fb != sm->prev_xray_cmd) ||
                         (raw->preheat_line_fb != sm->prev_preheat_cmd);
        sm->line_bad_ms = bad ? sm->line_bad_ms + dt : 0u;
        if (sm->line_bad_ms >= FE_LINE_MISMATCH_MS && sm->state != ST_FAULT) {
            if (sm->state == ST_EXPOSING) finish_exposure(sm, END_LINE_FAULT);
            else lock(sm, END_LINE_FAULT);
        }
    }

    /* Fokus rölesi: komut yalnız filaman sürülmüyorken (BEKLE / KİLİT / HATA)
     * değişir; röle yük altında anahtarlanmaz. Bırakık = büyük fokus. */
    const bool filament_driven = sm->state == ST_PREP || sm->state == ST_READY ||
                                 sm->state == ST_EXPOSING;
    if (!filament_driven) {
        const bool want_small = sm->params.focus == FOCUS_SMALL;
        if (want_small != sm->focus_cmd_small) {
            sm->focus_cmd_small = want_small;
            sm->focus_settle_ms = 0u;
        }
    }
    if (sm->focus_settle_ms < 0xFFFF0000u) sm->focus_settle_ms += dt;
    const bool focus_settled = sm->focus_settle_ms >= FE_FOCUS_SETTLE_MS;
    if (sm->cfg.focus_relay_readback) {
        const bool bad = focus_settled && raw->focus_fb_small != sm->focus_cmd_small;
        sm->focus_bad_ms = bad ? sm->focus_bad_ms + dt : 0u;
        if (sm->focus_bad_ms >= FE_FOCUS_MISMATCH_MS && sm->state != ST_FAULT) {
            if (sm->state == ST_EXPOSING) finish_exposure(sm, END_FOCUS_RELAY);
            else lock(sm, END_FOCUS_RELAY);
        }
    }

    /* kV / mA referans (DAC) geri okuması: bir önceki adımın komutu ile
     * karşılaştırılır. Beklemede referans 0 olmalı: takılı / yüksek kalan
     * DAC çıkışı ışınlamadan önce yakalanır. */
    if (sm->cfg.ref_readback) {
        const float tkv = fmaxf(FE_REF_TOL_KV, sm->prev_kv_ref * FE_REF_TOL_FRAC);
        const float tma = fmaxf(FE_REF_TOL_MA, sm->prev_ma_ref * FE_REF_TOL_FRAC);
        const bool bad = fabsf(raw->kv_ref_fb - sm->prev_kv_ref) > tkv ||
                         fabsf(raw->ma_ref_fb - sm->prev_ma_ref) > tma;
        sm->ref_bad_ms = bad ? sm->ref_bad_ms + dt : 0u;
        if (sm->ref_bad_ms >= FE_REF_MISMATCH_MS && sm->state != ST_FAULT) {
            if (sm->state == ST_EXPOSING) finish_exposure(sm, END_REF_READBACK);
            else lock(sm, END_REF_READBACK);
        }
    }

    /* Kanal tutarsızlığı: kısa devre veya kopuk kablo */
    sm->chan_bad_ms = chan_bad ? sm->chan_bad_ms + dt : 0u;
    if (sm->state != ST_FAULT && sm->chan_bad_ms >= FE_CHANNEL_MISMATCH_MS) {
        if (sm->state == ST_EXPOSING) finish_exposure(sm, END_INPUT_CHANNEL);
        else lock(sm, END_INPUT_CHANNEL);
    }

    /* --- Her durumda geçerli koşullar --- */
    if (sm->state != ST_FAULT) {
        if (!in->estop_ok) {
            if (sm->state == ST_EXPOSING) finish_exposure(sm, END_ESTOP);
            else lock(sm, END_ESTOP);
        } else if (in->gen_alarm || in->gen_over) {
            if (sm->state == ST_EXPOSING) finish_exposure(sm, END_GEN_ALARM);
            else if (sm->state != ST_IDLE || raw_any) lock(sm, END_GEN_ALARM);
        } else if ((!in->door_ok || !in->thermal_ok) &&
                   (sm->state == ST_PREP || sm->state == ST_READY || sm->state == ST_EXPOSING)) {
            const end_reason_t why = !in->door_ok ? END_INTERLOCK : END_TUBE_THERMAL;
            if (sm->state == ST_EXPOSING) finish_exposure(sm, why);
            else lock(sm, why);
        }
    }

    /* PC modu: yazılım bağlantısı zorunlu (arayüz tanımı rev. 0.4 §5).
     * Işınlamada koparsa ışınlama kesilir; hazırlıkta koparsa hazırlık iptal. */
    if (sm->cfg.pc_link_required && !in->pc_link_ok) {
        if (sm->state == ST_EXPOSING) {
            finish_exposure(sm, sm->cfg.pc_link_serial ? END_LINK_LOST_SERIAL : END_LINK_LOST);
        } else if (sm->state == ST_PREP || sm->state == ST_READY) {
            lock(sm, END_LINK_DOWN);
        }
    }

    switch (sm->state) {
    case ST_IDLE:
        if (in->hs_stage2) {
            lock(sm, END_EARLY_STAGE2);
        } else if (prep_held(sm, in)) {
            if (!in->door_ok || !in->thermal_ok) {
                lock(sm, !in->door_ok ? END_INTERLOCK : END_TUBE_THERMAL);
                break;
            }
            if (sm->cfg.pc_link_required && !in->pc_link_ok) {
                lock(sm, END_LINK_DOWN);   /* PC modu: bağlantı yokken hazırlık başlamaz */
                break;
            }
            if (!focus_settled ||
                (sm->cfg.focus_relay_readback && raw->focus_fb_small != sm->focus_cmd_small)) {
                break;   /* fokus rölesi oturmadan filaman sürülmez; birkaç ms bekler */
            }
            sm->last_tube_result = tube_check(&sm->tube, &sm->params);
            if (sm->last_tube_result != TUBE_OK) {
                lock(sm, END_TUBE_REJECT);
                break;
            }
            sm->last_reason = END_NONE;
            enter(sm, ST_PREP);
            sm->prep_total_ms = 0u;
            sm->warmup_count++;
        }
        break;

    case ST_PREP:
    case ST_READY: {
        if (in->detector_error) {
            lock(sm, END_DETECTOR_ERROR);
            break;
        }
        out->det_prep = true;
        out->preheat = true;
                out->lamp_prep = true;
        out->kv_ref = sm->params.kv;
        out->ma_ref = sm->params.ma;

        if (!is_fluoro(sm) && in->hs_stage2 && sm->state == ST_PREP) {
            lock(sm, END_EARLY_STAGE2);
            outputs_off(out);
            break;
        }
        if (!prep_held(sm, in)) {
            enter(sm, ST_IDLE);
            outputs_off(out);
            break;
        }
        /* 1. kademenin toplam süresi: değer değişikliği ısınmayı yeniden
         * başlatsa da flamanı korumak için toplam süre sınırlıdır */
        sm->prep_total_ms += dt;
        if (sm->prep_total_ms >= FE_PREP_HOLD_MAX_MS) {
            lock(sm, END_PREP_TIMEOUT);
            outputs_off(out);
            break;
        }

        if (sm->state == ST_PREP) {
            if (sm->t_state_ms >= FE_FILAMENT_WARMUP_MS) {
                const bool det_ok = !sm->cfg.detector_ready_required || in->detector_ready;
                if (fil_ok(sm, in) && det_ok) {
                    sm->state = ST_READY;   /* zamanlayıcıyı sıfırlamadan */
                } else if (!fil_ok(sm, in) &&
                           sm->t_state_ms >= FE_FILAMENT_WARMUP_MS + FE_FILAMENT_OK_TIMEOUT_MS) {
                    lock(sm, END_FILAMENT_TIMEOUT);
                    outputs_off(out);
                } else if (!det_ok &&
                           sm->t_state_ms >= FE_FILAMENT_WARMUP_MS + FE_DETECTOR_READY_TIMEOUT_MS) {
                    lock(sm, END_DETECTOR_TIMEOUT);
                    outputs_off(out);
                }
            }
            break;
        }

        /* ST_READY */
        out->ready_indicator = true;
        if (!fil_ok(sm, in)) {
            lock(sm, END_FILAMENT_LOST);
            outputs_off(out);
            break;
        }
        if (sm->cfg.detector_ready_required && !in->detector_ready) {
            sm->state = ST_PREP;      /* dedektör hazır değil: hazır lambası söner */
            out->ready_indicator = false;
            break;
        }
        const bool expose_req = is_fluoro(sm) ? in->pedal : in->hs_stage2;
        if (!expose_req) {
            sm->det_wait_ms = 0u;
        }
        if (expose_req && sm->cfg.wait_detector_enable) {
            /* Senkron dedektör: önce ışınlama isteği, izin gelince ışınlama */
            out->det_exp_req = true;
            if (!in->detector_enable) {
                sm->det_wait_ms += dt;
                if (sm->det_wait_ms >= FE_DETECTOR_ENABLE_TIMEOUT_MS) {
                    sm->det_wait_ms = 0u;
                    lock(sm, END_DETECTOR_TIMEOUT);
                    outputs_off(out);
                }
                break;
            }
            sm->det_wait_ms = 0u;
        }
        if (expose_req) {
            sm->last_tube_result = tube_check(&sm->tube, &sm->params);
            if (sm->last_tube_result != TUBE_OK) {
                lock(sm, END_TUBE_REJECT);
                outputs_off(out);
                break;
            }
            sm->t_expose_ms = 0u;
            sm->tol_kv_ms = sm->tol_ma_ms = 0u;
            sm->kv_sum = sm->ma_sum = sm->mas_acc = 0.0f;
            sm->n_samples = 0u;
            sm->irr_active = true;
            sm->irr_ms = sm->irr_after_ms = 0u;
            sm->irr_thr_kv = sm->params.kv * FE_IRR_THRESHOLD_FRAC;
            enter(sm, ST_EXPOSING);
            out->rl5_series = true;
        }
        break;
    }

    case ST_EXPOSING: {
        out->preheat = true;
                out->rl5_series = true;
        out->kv_ref = sm->params.kv;
        out->ma_ref = sm->params.ma;
        out->buzzer = true;

        const bool held = is_fluoro(sm) ? in->pedal : (in->hs_stage1 && in->hs_stage2);
        end_reason_t reason = END_NONE;
        out->det_prep = true;
        out->det_exp_req = sm->cfg.wait_detector_enable;

        if (in->detector_error) {
            reason = END_DETECTOR_ERROR;
        } else if (sm->cfg.wait_detector_enable && !in->detector_enable) {
            reason = END_DETECTOR_WINDOW;
        } else if (!held) {
            /* Floroskopide pedalı bırakmak normal bitiştir; radyografide
             * süre dolmadan bırakmak "erken bırakıldı" olarak raporlanır. */
            reason = is_fluoro(sm) ? END_RELEASED : END_EARLY_RELEASE;
        } else if (!fil_ok(sm, in)) {
            reason = END_FILAMENT_LOST;
        } else if (sm->t_state_ms > SM_RL5_SETTLE_MS) {
            /* RL5 oturdu: X-RAY ON aktif */
            if (!in->backup_ok) {
                reason = END_BACKUP_TIMER;
            } else {
                sm->t_expose_ms += dt;
                sm->kv_sum += in->kv_fb;
                sm->ma_sum += in->ma_fb;
                sm->n_samples++;
                sm->mas_acc += in->ma_fb * ((float)dt / 1000.0f);
                reason = check_tolerance(sm, in, dt);
                if (reason == END_NONE && sm->t_expose_ms >= sm->params.time_ms) {
                    reason = END_TIME_DONE;
                }
                if (reason == END_NONE) {
                    out->xray_on = true;
                    out->lamp_exposure = true;
                }
            }
        }

        if (reason != END_NONE) {
            out->xray_on = false;
            out->lamp_exposure = false;
            out->kv_ref = 0.0f;
            out->ma_ref = 0.0f;
            out->preheat = false;
            out->buzzer = false;
            finish_exposure(sm, reason);
            out->rl5_series = true;   /* RL5 X-RAY ON'dan sonra açılır */
        }
        break;
    }

    case ST_LOCKED:
        if (sm->rl5_hold_ms > 0u) {
            out->rl5_series = true;
            sm->rl5_hold_ms = (sm->rl5_hold_ms > dt) ? sm->rl5_hold_ms - dt : 0u;
        }
        /* Normal olmayan kilitlerde kısa uyarı sesi */
        if (sm->last_reason != END_TIME_DONE && sm->last_reason != END_RELEASED &&
            sm->last_reason != END_EARLY_STAGE2 &&
            sm->t_state_ms <= 300u) {
            out->buzzer = true;
        }
        if (!raw_any) {
            enter(sm, ST_IDLE);
        } else {
            /* Çekimden sonra 10 s bırakılmayan buton takılı sayılır. Diğer
             * kilitlerde (ör. E101) operatör butonu bırakmayı unutmuş olabilir;
             * yanlış "servis" hatası vermemek için 60 s beklenir. */
            const bool after_exposure = sm->last_reason == END_TIME_DONE ||
                                        sm->last_reason == END_EARLY_RELEASE ||
                                        sm->last_reason == END_RELEASED ||
                                        sm->last_reason == END_BUTTON_STUCK;
            sm->stuck_ms += dt;
            if (sm->stuck_ms >= (after_exposure ? FE_BUTTON_STUCK_MS : FE_BUTTON_HELD_MAX_MS)) {
                lock(sm, END_BUTTON_STUCK);   /* HATA: buton/pedal takılı */
            }
        }
        break;

    case ST_FAULT:
        if (sm->rl5_hold_ms > 0u) {
            out->rl5_series = true;
            sm->rl5_hold_ms = (sm->rl5_hold_ms > dt) ? sm->rl5_hold_ms - dt : 0u;
        }
        break;
    }

    if (sm->fluoro_alert) {
        out->buzzer = true;
    }

    /* Anot ısı modeli: gerçek güç, yalnızca X-RAY ON sırasında */
    const float power = out->xray_on ? in->kv_fb * in->ma_fb : 0.0f;
    tube_tick(&sm->tube, power, dt);

    /* Fokus rölesi her durumda komutu tutar (outputs_off sonrası) */
    out->focus_small = sm->focus_cmd_small;

    /* Inter-Lock: jeneratörün ikinci izin kanalı */
    out->gen_interlock = out->preheat || out->xray_on;

    sm->prev_xray_cmd = out->xray_on && out->rl5_series;
    sm->prev_preheat_cmd = out->preheat;
    sm->prev_kv_ref = out->kv_ref;
    sm->prev_ma_ref = out->ma_ref;
}

bool sm_ack_fault(exposure_sm_t *sm)
{
    if (sm->state != ST_FAULT) {
        return false;
    }
    /* Not: acil stop hâlâ basılıysa bir sonraki adımda tekrar HATA olur;
     * acil stop bırakıldıktan sonra da sistem kendiliğinden çalışmaz,
     * bu onay (elle reset) gerekir. */
    enter(sm, ST_LOCKED);   /* tam bırakma yine gerekir */
    return true;
}

void sm_fluoro_reset_cumulative(exposure_sm_t *sm)
{
    sm->fluoro_cumulative_ms = 0u;
    sm->fluoro_alert = false;
}

const char *sm_state_text(sm_state_t s)
{
    switch (s) {
    case ST_IDLE:     return "BEKLE";
    case ST_PREP:     return "HAZIRLIK";
    case ST_READY:    return "HAZIR";
    case ST_EXPOSING: return "ISINLAMA";
    case ST_LOCKED:   return "BUTONU BIRAKIN";
    case ST_FAULT:    return "HATA";
    }
    return "?";
}

const char *sm_reason_text(end_reason_t r)
{
    switch (r) {
    case END_NONE:             return "-";
    case END_TIME_DONE:        return "Tamamlandi";
    case END_RELEASED:         return "Buton birakildi";
    case END_INTERLOCK:        return "Kapi acik";
    case END_TUBE_THERMAL:     return "Tup termigi acik";
    case END_ESTOP:            return "Acil stop";
    case END_FILAMENT_LOST:    return "Filaman sinyali kayboldu";
    case END_GEN_ALARM:        return "Jenerator alarmi";
    case END_KV_LOW:           return "kV dusuk (%85 alti)";
    case END_KV_HIGH:          return "kV yuksek (%110 ustu)";
    case END_MA_TOL:           return "mA tolerans disi";
    case END_BACKUP_TIMER:     return "Yedek zamanlayici kesti";
    case END_EARLY_STAGE2:     return "Hazir olmadan 2. kademe";
    case END_PREP_TIMEOUT:     return "Hazirlik cok uzun tutuldu";
    case END_FILAMENT_TIMEOUT: return "Filaman hazir olmadi";
    case END_DETECTOR_TIMEOUT: return "Dedektor hazir olmadi";
    case END_TUBE_REJECT:      return "Tup korumasi izin vermedi";
    case END_BUTTON_STUCK:     return "Buton veya pedal takili";
    case END_INPUT_CHANNEL:    return "Buton/pedal kablo arizasi";
    case END_ESTOP_CHANNEL:    return "Acil stop kablo/kontak arizasi";
    case END_LINE_FAULT:       return "X-RAY ON / PreHeat hatti arizasi";
    case END_EARLY_RELEASE:    return "Buton erken birakildi";
    case END_TIME_DEV:         return "Isinlama suresi sapmasi";
    case END_DETECTOR_WINDOW:  return "Dedektor penceresi erken kapandi";
    case END_DETECTOR_ERROR:   return "Dedektor hatasi";
    case END_LINK_DOWN:        return "Yazilim baglantisi yok";
    case END_LINK_LOST:        return "Isinlama baglanti koptugu icin kesildi";
    case END_LINK_LOST_SERIAL: return "Isinlama RS-232 baglantisi koptugu icin kesildi";
    case END_FOCUS_RELAY:      return "Fokus rolesi uyusmazligi";
    case END_REF_READBACK:     return "kV / mA referans cikisi uyusmazligi";
    case END__COUNT:           break;
    }
    return "?";
}
