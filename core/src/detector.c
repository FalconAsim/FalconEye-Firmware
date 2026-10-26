#include "detector.h"
#include <stddef.h>

/*
 * Hazır profiller. Sinyal adları ve zamanlamaları üreticiden üreticiye
 * değişir; aşağıdakiler genel şablonlardır. PZ Medical 4343Z profili,
 * senkron arayüz dokümanı geldiğinde eklenecek.
 */
static const det_profile_t builtin[] = {
    { "Kapali (CR / film)", DET_PROFILE_OFF, false,
      { DET_IN_NONE, DET_IN_NONE, DET_IN_NONE }, { false, false, false },
      { DET_OUT_NONE, DET_OUT_NONE, DET_OUT_NONE }, { false, false, false } },
    { "AED (DR hazir bilgisi yok)", DET_PROFILE_AED, false,
      { DET_IN_NONE, DET_IN_NONE, DET_IN_NONE }, { false, false, false },
      { DET_OUT_NONE, DET_OUT_NONE, DET_OUT_NONE }, { false, false, false } },
    { "AED + DR hazir bilgisi (PZ 3025ZF)", DET_PROFILE_AED, true,
      { DET_IN_NONE, DET_IN_NONE, DET_IN_NONE }, { false, false, false },
      { DET_OUT_NONE, DET_OUT_NONE, DET_OUT_NONE }, { false, false, false } },
    { "Senkron 2 hat (istek / izin)", DET_PROFILE_SYNC, false,
      { DET_IN_ENABLE, DET_IN_NONE, DET_IN_NONE }, { false, false, false },
      { DET_OUT_EXP_REQ, DET_OUT_NONE, DET_OUT_NONE }, { false, false, false } },
    { "Senkron 3+3 hat (hazirlik / hazir / istek / izin / hata)", DET_PROFILE_SYNC, false,
      { DET_IN_READY, DET_IN_ENABLE, DET_IN_ERROR }, { false, false, false },
      { DET_OUT_PREP, DET_OUT_EXP_REQ, DET_OUT_XRAY_ON }, { false, false, false } },
};

uint32_t det_builtin_count(void) { return (uint32_t)(sizeof(builtin) / sizeof(builtin[0])); }

const det_profile_t *det_builtin(uint32_t i)
{
    return i < det_builtin_count() ? &builtin[i] : NULL;
}

static bool has_in(const det_profile_t *p, det_in_func_t f)
{
    for (int i = 0; i < DET_LINES; i++) if (p->in_func[i] == f) return true;
    return false;
}

static bool has_out(const det_profile_t *p, det_out_func_t f)
{
    for (int i = 0; i < DET_LINES; i++) if (p->out_func[i] == f) return true;
    return false;
}

bool det_profile_valid(const det_profile_t *p)
{
    if (p == NULL) return false;
    for (int i = 0; i < DET_LINES; i++) {
        for (int j = i + 1; j < DET_LINES; j++) {
            if (p->in_func[i] != DET_IN_NONE && p->in_func[i] == p->in_func[j]) return false;
            if (p->out_func[i] != DET_OUT_NONE && p->out_func[i] == p->out_func[j]) return false;
        }
    }
    if (p->kind == DET_PROFILE_SYNC) {
        if (!has_in(p, DET_IN_ENABLE) || !has_out(p, DET_OUT_EXP_REQ)) return false;
    } else {
        /* KAPALI ve AED'de J19 kullanılmaz */
        for (int i = 0; i < DET_LINES; i++) {
            if (p->in_func[i] != DET_IN_NONE || p->out_func[i] != DET_OUT_NONE) return false;
        }
    }
    return true;
}

void det_apply_config(const det_profile_t *p, sm_config_t *cfg)
{
    switch (p->kind) {
    case DET_PROFILE_OFF:
        cfg->detector_ready_required = false;
        cfg->wait_detector_enable = false;
        break;
    case DET_PROFILE_AED:
        cfg->detector_ready_required = p->ready_from_pc;
        cfg->wait_detector_enable = false;
        break;
    case DET_PROFILE_SYNC:
        cfg->detector_ready_required = has_in(p, DET_IN_READY);
        cfg->wait_detector_enable = true;
        break;
    }
}

void det_map_inputs(const det_profile_t *p, const bool raw_in[DET_LINES], bool pc_ready,
                    sm_inputs_t *in)
{
    in->detector_ready = false;
    in->detector_enable = false;
    in->detector_error = false;

    if (p->kind == DET_PROFILE_AED) {
        in->detector_ready = p->ready_from_pc ? pc_ready : true;
        return;
    }
    if (p->kind != DET_PROFILE_SYNC) {
        in->detector_ready = true;
        return;
    }
    for (int i = 0; i < DET_LINES; i++) {
        const bool active = p->in_active_low[i] ? !raw_in[i] : raw_in[i];
        switch (p->in_func[i]) {
        case DET_IN_READY:  in->detector_ready = active; break;
        case DET_IN_ENABLE: in->detector_enable = active; break;
        case DET_IN_ERROR:  in->detector_error = active; break;
        case DET_IN_NONE:   break;
        }
    }
    if (!has_in(p, DET_IN_READY)) in->detector_ready = true;
}

void det_map_outputs(const det_profile_t *p, const sm_outputs_t *out, bool raw_out[DET_LINES])
{
    for (int i = 0; i < DET_LINES; i++) {
        bool active = false;
        if (p->kind == DET_PROFILE_SYNC) {
            switch (p->out_func[i]) {
            case DET_OUT_PREP:    active = out->det_prep; break;
            case DET_OUT_EXP_REQ: active = out->det_exp_req; break;
            case DET_OUT_XRAY_ON: active = out->xray_on; break;
            case DET_OUT_NONE:    active = false; break;
            }
        }
        /* Kullanılmayan hat her zaman açık (potansiyelsiz, pasif) */
        raw_out[i] = (p->out_func[i] == DET_OUT_NONE || p->kind != DET_PROFILE_SYNC)
                         ? false
                         : (p->out_active_open[i] ? !active : active);
    }
}
