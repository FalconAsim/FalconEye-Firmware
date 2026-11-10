#include "km_proto.h"
#include <string.h>

static void p16(uint8_t *d, uint16_t v) { d[0] = (uint8_t)(v & 0xFFu); d[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *d, uint32_t v) { p16(d, (uint16_t)(v & 0xFFFFu)); p16(d + 2, (uint16_t)(v >> 16)); }
static uint16_t g16(const uint8_t *d) { return (uint16_t)(d[0] | ((uint16_t)d[1] << 8)); }
static uint32_t g32(const uint8_t *d) { return (uint32_t)g16(d) | ((uint32_t)g16(d + 2) << 16); }

uint8_t km_pack_win(const km_win_t *m, uint8_t *d)
{
    d[0] = m->seq; d[1] = m->open ? 1u : 0u; p16(d + 2, m->remain_ms); d[4] = m->src; d[5] = m->flags; p16(d + 6, m->cfg_crc);
    return 8;
}
bool km_unpack_win(const uint8_t *d, uint8_t len, km_win_t *m)
{
    if (len != 8 || d[1] > 1u || d[4] > KM_SRC_BOTH) return false;
    m->seq = d[0]; m->open = d[1] == 1u; m->remain_ms = g16(d + 2); m->src = d[4]; m->flags = d[5]; m->cfg_crc = g16(d + 6);
    return true;
}
uint8_t km_pack_revt(const km_revt_t *m, uint8_t *d) { p32(d, m->t_ms); p16(d + 4, m->rr_ms); p16(d + 6, (uint16_t)m->amp_uv); return 8; }
bool km_unpack_revt(const uint8_t *d, uint8_t len, km_revt_t *m)
{
    if (len != 8) return false;
    m->t_ms = g32(d); m->rr_ms = g16(d + 4); m->amp_uv = (int16_t)g16(d + 6); return true;
}
uint8_t km_pack_respevt(const km_respevt_t *m, uint8_t *d) { p32(d, m->t_ms); p16(d + 4, m->period_ms); d[6] = m->phase; d[7] = m->level; return 8; }
bool km_unpack_respevt(const uint8_t *d, uint8_t len, km_respevt_t *m)
{
    if (len != 8) return false;
    m->t_ms = g32(d); m->period_ms = g16(d + 4); m->phase = d[6]; m->level = d[7]; return true;
}
uint8_t km_pack_cfg(const km_cfg_t *m, uint8_t *d)
{
    d[0] = m->src; d[1] = m->rph; d[2] = m->species; d[3] = m->flags; p16(d + 4, m->dly_ms); p16(d + 6, m->win_ms); return 8;
}
bool km_unpack_cfg(const uint8_t *d, uint8_t len, km_cfg_t *m)
{
    if (len != 8) return false;
    m->src = d[0]; m->rph = d[1]; m->species = d[2]; m->flags = d[3]; m->dly_ms = g16(d + 4); m->win_ms = g16(d + 6); return true;
}
uint8_t km_pack_cfg_ack(const km_cfg_ack_t *m, uint8_t *d)
{
    d[0] = m->result; d[1] = 0; p16(d + 2, m->cfg_crc); p16(d + 4, m->min_dly_ms); p16(d + 6, m->max_win_ms); return 8;
}
bool km_unpack_cfg_ack(const uint8_t *d, uint8_t len, km_cfg_ack_t *m)
{
    if (len != 8) return false;
    m->result = d[0]; m->rsv = 0; m->cfg_crc = g16(d + 2); m->min_dly_ms = g16(d + 4); m->max_win_ms = g16(d + 6); return true;
}
uint8_t km_pack_cmd(const km_cmd_msg_t *m, uint8_t *d) { d[0] = m->cmd; d[1] = m->arg; p16(d + 2, 0); p32(d + 4, m->magic); return 8; }
bool km_unpack_cmd(const uint8_t *d, uint8_t len, km_cmd_msg_t *m)
{
    if (len != 8) return false;
    m->cmd = d[0]; m->arg = d[1]; m->rsv = 0; m->magic = g32(d + 4); return true;
}
uint8_t km_pack_cmd_ack(const km_cmd_ack_t *m, uint8_t *d) { memset(d, 0, 8); d[0] = m->cmd; d[1] = m->result; return 8; }
bool km_unpack_cmd_ack(const uint8_t *d, uint8_t len, km_cmd_ack_t *m)
{
    if (len != 8) return false;
    m->cmd = d[0]; m->result = d[1]; return true;
}
uint8_t km_pack_status(const km_status_t *m, uint8_t *d)
{
    d[0] = m->src; d[1] = m->ecg_q; d[2] = m->lead_mask; d[3] = m->rsrc; p16(d + 4, m->hr_bpm); p16(d + 6, m->rr_x10);
    p16(d + 8, (uint16_t)m->rbase); p16(d + 10, m->cfg_crc); d[12] = m->err; d[13] = m->wave_on; p16(d + 14, m->uptime_s);
    return 16;
}
bool km_unpack_status(const uint8_t *d, uint8_t len, km_status_t *m)
{
    if (len != 16) return false;
    m->src = d[0]; m->ecg_q = d[1]; m->lead_mask = d[2]; m->rsrc = d[3]; m->hr_bpm = g16(d + 4); m->rr_x10 = g16(d + 6);
    m->rbase = (int16_t)g16(d + 8); m->cfg_crc = g16(d + 10); m->err = d[12]; m->wave_on = d[13]; m->uptime_s = g16(d + 14);
    return true;
}
uint8_t km_pack_wave(const km_wave_t *m, uint8_t *d)
{
    p32(d, m->t_ms);
    for (int i = 0; i < 10; i++) p16(d + 4 + 2 * i, (uint16_t)m->s[i]);
    return 24;
}
bool km_unpack_wave(const uint8_t *d, uint8_t len, km_wave_t *m)
{
    if (len != 24) return false;
    m->t_ms = g32(d);
    for (int i = 0; i < 10; i++) m->s[i] = (int16_t)g16(d + 4 + 2 * i);
    return true;
}
uint8_t km_pack_hb(const km_hb_t *m, uint8_t *d) { d[0] = m->seq; d[1] = m->wave_on; p16(d + 2, 0); p32(d + 4, m->a_time_ms); return 8; }
bool km_unpack_hb(const uint8_t *d, uint8_t len, km_hb_t *m)
{
    if (len != 8) return false;
    m->seq = d[0]; m->wave_on = d[1]; m->a_time_ms = g32(d + 4); return true;
}
uint8_t km_pack_version(const km_version_t *m, uint8_t *d)
{
    memset(d, 0, 16);
    d[0] = m->proto; d[1] = m->fw_major; d[2] = m->fw_minor; d[3] = m->fw_patch; d[4] = m->hw_rev; p32(d + 8, m->uid);
    return 16;
}
bool km_unpack_version(const uint8_t *d, uint8_t len, km_version_t *m)
{
    if (len != 16) return false;
    m->proto = d[0]; m->fw_major = d[1]; m->fw_minor = d[2]; m->fw_patch = d[3]; m->hw_rev = d[4]; m->uid = g32(d + 8); return true;
}

uint16_t km_cfg_crc(const km_cfg_t *c)
{
    uint8_t d[8];
    (void)km_pack_cfg(c, d);
    uint16_t crc = 0xFFFFu;
    for (int i = 0; i < 8; i++) {
        crc ^= (uint16_t)((uint16_t)d[i] << 8);
        for (int b = 0; b < 8; b++) crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

void km_win_rx_init(km_win_rx_t *r) { memset(r, 0, sizeof *r); }

void km_win_rx_update(km_win_rx_t *r, const km_win_t *m, uint32_t now_ms) { r->last = *m; r->rx_ms = now_ms; r->have = true; }

bool km_win_rx_usable(const km_win_rx_t *r, uint32_t now_ms, uint16_t need_ms, uint16_t expect_cfg_crc)
{
    if (!r->have || !r->last.open) return false;
    uint32_t age = now_ms - r->rx_ms;
    if (age > KM_WIN_MAX_AGE_MS) return false;                 /* mesaj kaybı / modül sustu → kapalı */
    if (r->last.cfg_crc != expect_cfg_crc) return false;       /* modül başka ayarla çalışıyor → kapalı */
    if (r->last.remain_ms < age) return false;
    return (uint32_t)(r->last.remain_ms - age) >= need_ms;
}
