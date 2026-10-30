#include "service.h"

static bool sm_idle(const exposure_sm_t *sm)
{
    return sm->state != ST_PREP && sm->state != ST_READY && sm->state != ST_EXPOSING;
}

bool svc_apply_boot(exposure_sm_t *sm, const nv_settings_t *st)
{
    bool ok = true;
    if (!tube_select(&sm->tube, (tube_insert_t)st->tube_insert)) {
        tube_select(&sm->tube, TUBE_INSERT_DF161SBR);
        ok = false;
    }
    sm->cfg.fluoro_enabled = st->fluoro_enabled != 0u;
    sm->cfg.dap_meter_enabled = st->dap_meter_enabled != 0u;
    sm->cfg.large_focus_off = st->large_focus_off != 0u;
    sm_load_boot_defaults(sm);
    return ok;
}

void svc_init(svc_t *svc, exposure_sm_t *sm, nvstore_t *nv, nv_data_t *data, uint32_t pin)
{
    svc->sm = sm;
    svc->nv = nv;
    svc->data = data;
    svc->pin = pin;
    svc->active = false;
    svc->bad_tries = 0u;
    svc->lockout_ms = 0u;
    svc->idle_ms = 0u;
}

svc_result_t svc_enter(svc_t *svc, uint32_t pin)
{
    if (svc->lockout_ms > 0u) return SVC_ERR_LOCKED_OUT;
    if (pin != svc->pin) {
        if (++svc->bad_tries >= SVC_PIN_TRIES) {
            svc->bad_tries = 0u;
            svc->lockout_ms = SVC_LOCKOUT_MS;
        }
        return SVC_ERR_BAD_PIN;
    }
    svc->bad_tries = 0u;
    svc->active = true;
    svc->idle_ms = 0u;
    return SVC_OK;
}

void svc_exit(svc_t *svc) { svc->active = false; }

void svc_tick(svc_t *svc, uint32_t dt_ms)
{
    svc->lockout_ms = svc->lockout_ms > dt_ms ? svc->lockout_ms - dt_ms : 0u;
    if (svc->active) {
        svc->idle_ms += dt_ms;
        if (svc->idle_ms >= SVC_IDLE_TIMEOUT_MS) svc->active = false;
    }
}

static svc_result_t pre(svc_t *svc)
{
    if (!svc->active) return SVC_ERR_NOT_ACTIVE;
    svc->idle_ms = 0u;
    if (!sm_idle(svc->sm)) return SVC_ERR_BUSY;
    return SVC_OK;
}

/* Yeni ayarı kaydeder; başarısızsa RAM'deki ayarı eski haline getirir. */
static svc_result_t commit(svc_t *svc, const nv_settings_t *old)
{
    if (nv_save(svc->nv, svc->data)) return SVC_OK;
    svc->data->settings = *old;
    return SVC_ERR_SAVE;
}

svc_result_t svc_set_fluoro(svc_t *svc, bool on)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    const nv_settings_t old = svc->data->settings;
    svc->data->settings.fluoro_enabled = on ? 1u : 0u;
    const svc_result_t c = commit(svc, &old);
    if (c != SVC_OK) return c;
    svc->sm->cfg.fluoro_enabled = on;
    if (!on && svc->sm->params.mode == MODE_FLUORO) {
        sm_load_boot_defaults(svc->sm);   /* radyografiye dön */
    }
    if (!on) svc->sm->have_saved_fluoro = false;
    return SVC_OK;
}

svc_result_t svc_set_dap_meter(svc_t *svc, bool on)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    const nv_settings_t old = svc->data->settings;
    svc->data->settings.dap_meter_enabled = on ? 1u : 0u;
    const svc_result_t c = commit(svc, &old);
    if (c != SVC_OK) return c;
    svc->sm->cfg.dap_meter_enabled = on;
    return SVC_OK;
}

svc_result_t svc_set_insert(svc_t *svc, tube_insert_t id)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    if ((unsigned)id >= (unsigned)TUBE_INSERT_COUNT) return SVC_ERR_PARAM;
    const nv_settings_t old = svc->data->settings;
    svc->data->settings.tube_insert = (uint8_t)id;
    const svc_result_t c = commit(svc, &old);
    if (c != SVC_OK) return c;
    tube_select(&svc->sm->tube, id);
    sm_load_boot_defaults(svc->sm);       /* yeni insertin sınırlarına göre */
    return SVC_OK;
}

svc_result_t svc_set_large_focus(svc_t *svc, bool on)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    const nv_settings_t old = svc->data->settings;
    svc->data->settings.large_focus_off = (uint8_t)(on ? 0u : 1u);
    const svc_result_t c = commit(svc, &old);
    if (c != SVC_OK) return c;
    svc->sm->cfg.large_focus_off = !on;
    if (!on && svc->sm->params.focus == FOCUS_LARGE && tube_has_small_focus(&svc->sm->tube)) {
        sm_load_boot_defaults(svc->sm);   /* küçük fokusa dön */
    }
    return SVC_OK;
}

static float maxf2(float a, float b) { return a > b ? a : b; }

static bool last_exposure_ok(const exposure_sm_t *sm)
{
    return sm->last_record.valid && sm->last_record.reason == END_TIME_DONE &&
           sm->last_record.set.mode == MODE_RADIOGRAPHY;
}

/* Kalibrasyonu kaydeder; yazılamazsa eski tabloya döner */
static svc_result_t commit_cal(svc_t *svc, const calib_t *old)
{
    if (nv_save(svc->nv, svc->data)) return SVC_OK;
    svc->data->calib = *old;
    return SVC_ERR_SAVE;
}

svc_result_t svc_cal_kv(svc_t *svc, float measured_kv, cal_result_t *res, float *v_new)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    if (!last_exposure_ok(svc->sm)) return SVC_ERR_NO_EXPOSURE;
    const float set = svc->sm->last_record.set.kv;
    const calib_t old = svc->data->calib;
    const cal_result_t cr = calib_correct_kv(&svc->data->calib, set, measured_kv,
                                             maxf2(SVC_CAL_KV_TOL_ABS, SVC_CAL_KV_TOL_FRAC * set), v_new);
    if (res) *res = cr;
    if (cr != CAL_OK && cr != CAL_IN_TOL) return SVC_ERR_PARAM;
    return commit_cal(svc, &old);
}

svc_result_t svc_cal_ma(svc_t *svc, float measured_ma, cal_result_t *res, float *v_new)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    if (!last_exposure_ok(svc->sm)) return SVC_ERR_NO_EXPOSURE;
    const exposure_params_t *p = &svc->sm->last_record.set;
    const calib_t old = svc->data->calib;
    const cal_result_t cr = calib_correct_ma(&svc->data->calib, (int)p->focus, p->kv, p->ma, measured_ma,
                                             maxf2(SVC_CAL_MA_TOL_ABS, SVC_CAL_MA_TOL_FRAC * p->ma), v_new);
    if (res) *res = cr;
    if (cr != CAL_OK && cr != CAL_IN_TOL) return SVC_ERR_PARAM;
    return commit_cal(svc, &old);
}

svc_result_t svc_cal_reset(svc_t *svc, bool kv, bool ma)
{
    const svc_result_t r = pre(svc);
    if (r != SVC_OK) return r;
    const calib_t old = svc->data->calib;
    calib_t def;
    calib_default(&def);
    if (kv) { svc->data->calib.kv_to_v = def.kv_to_v; svc->data->calib.done &= (uint8_t)~CAL_DONE_KV; }
    if (ma) {
        svc->data->calib.ma[0] = def.ma[0]; svc->data->calib.ma[1] = def.ma[1];
        svc->data->calib.done &= (uint8_t)~(CAL_DONE_MA_LARGE | CAL_DONE_MA_SMALL);
    }
    return commit_cal(svc, &old);
}

const char *svc_result_text(svc_result_t r)
{
    switch (r) {
    case SVC_OK:             return "Kaydedildi";
    case SVC_ERR_NOT_ACTIVE: return "Servis moduna girilmedi";
    case SVC_ERR_BAD_PIN:    return "PIN yanlış";
    case SVC_ERR_LOCKED_OUT: return "Çok fazla yanlış PIN, 60 s bekleyin";
    case SVC_ERR_BUSY:       return "Hazırlık / ışınlama sürerken ayar değişmez";
    case SVC_ERR_PARAM:      return "Geçersiz değer";
    case SVC_ERR_SAVE:       return "Kalıcı belleğe yazılamadı, ayar değişmedi";
    case SVC_ERR_NO_EXPOSURE: return "Önce bu değerle tam süreli bir çekim yapın";
    }
    return "?";
}
