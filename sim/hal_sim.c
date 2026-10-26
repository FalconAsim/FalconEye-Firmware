#include "hal_sim.h"
#include "hs_line.h"
#include <string.h>

void sim_init(sim_t *s)
{
    memset(s, 0, sizeof(*s));
    s->door_ok = true;
    s->thermal_ok = true;
    s->estop_ok = true;
    s->backup_ok = true;
    s->detector_ready = true;
    s->filament_ready_after_ms = 3500u;   /* gerçek flaman 4 s'den kısa sürede hazır */
    s->rise_tau_ms = 1.0f;   /* yüksek frekanslı jeneratör: ~1 ms yükselme */
}

static void gen_model(sim_t *s, const sm_outputs_t *o)
{
    /* Flaman */
    s->filament_heat_ms = o->preheat ? s->filament_heat_ms + 1u : 0u;

    /* Jeneratör modeli: X-RAY ON hattı aktif VE Inter-Lock kapalıysa ışınlar.
     * Hat aktif = (PhotoMOS VE RL5) veya hattın GND'ye kısa devresi. */
    const bool line = ((o->xray_on && o->rl5_series) || s->xray_line_short) && !s->xray_line_open;
    const bool hv = line && o->gen_interlock;
    if (s->xray_line_short && hv && !(o->xray_on && o->rl5_series)) s->hv_from_short = true;
    const float kv_target = hv ? o->kv_ref * (1.0f - s->kv_gain_error) : 0.0f;
    const float ma_target = hv ? o->ma_ref : 0.0f;
    const float a = 1.0f / s->rise_tau_ms;
    s->kv += (kv_target - s->kv) * a;
    s->ma += (ma_target - s->ma) * a;

    if (o->xray_on) s->xray_on_ms++;
    if (o->xray_on && !o->rl5_series) s->xray_without_rl5 = true;
}

void sim_run(sim_t *s, exposure_sm_t *sm, uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        sm_inputs_t in = {
            .hs_stage1 = s->hs1 || s->hs1_no_short,
            .hs_stage1_nc = !s->hs1,
            .hs_stage2 = s->hs2 || s->hs2_no_short,
            .hs_stage2_nc = !s->hs2 && !s->hs2_nc_open,
            .pedal = s->pedal,
            .pedal_b = s->pedal || s->pedal_b_short,
            .filament_ok = !s->filament_broken &&
                           s->filament_heat_ms >= s->filament_ready_after_ms,
            .door_ok = s->door_ok,
            .thermal_ok = s->thermal_ok,
            .estop_ok = s->estop_ok,
            .estop_ok_b = s->estop_ok || s->estop_ch2_short,
            .gen_alarm = s->gen_alarm,
            .gen_over = s->gen_over,
            .backup_ok = s->backup_ok,
            .detector_ready = s->detector_ready,
            .detector_enable = s->detector_enable,
            .detector_error = s->detector_error,
            .xray_line_fb = ((s->last_out.xray_on && s->last_out.rl5_series) || s->xray_line_short)
                            && !s->xray_line_open,
            .preheat_line_fb = s->last_out.preheat,
            .kv_fb = s->kv,
            .ma_fb = s->ma,
        };
        if (s->use_hs_adc) {
            hs_line_map(hs_line_classify(s->hs1_adc), &in.hs_stage1, &in.hs_stage1_nc);
            hs_line_map(hs_line_classify(s->hs2_adc), &in.hs_stage2, &in.hs_stage2_nc);
        }
        sm_outputs_t out;
        sm_step(sm, &in, &out, 1u);
        gen_model(s, &out);
        s->last_out = out;
        s->now_ms++;
    }
}
