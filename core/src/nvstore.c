#include "nvstore.h"
#include "fe_config.h"
#include <string.h>

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t seq;
} nv_header_t;

typedef struct {
    nv_header_t h;
    nv_data_t d;
    uint32_t crc;      /* başlık + veri üzerinden */
} nv_record_t;

/* Sürüm 1 düzeni (firmware v0.8) – yalnız okuma ve dönüştürme için */
typedef struct {
    float gen_power_max_w;
    uint8_t language, detector_profile, fluoro_enabled, pc_required;
    uint32_t exposure_count;
} nv_settings_v1_t;
typedef struct { calib_t calib; nv_settings_v1_t settings; } nv_data_v1_t;
typedef struct { nv_header_t h; nv_data_v1_t d; uint32_t crc; } nv_record_v1_t;

_Static_assert(sizeof(nv_record_t) <= NV_SLOT_SIZE, "kayit bir slota sigmali");

uint32_t nv_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void nv_defaults(nv_data_t *d)
{
    memset(d, 0, sizeof(*d));
    calib_default(&d->calib);
    d->settings.gen_power_max_w = FE_GEN_POWER_MAX_W;
    d->settings.language = 0;
    d->settings.detector_profile = DET_PROFILE_AED;
    d->settings.fluoro_enabled = 0;
    d->settings.pc_required = 0;
    d->settings.tube_insert = 0;          /* TUBE_INSERT_DF161SBR */
    d->settings.dap_meter_enabled = 0;    /* varsayılan: DAP'ı yazılım hesaplar */
    d->settings.large_focus_off = 1;      /* DF-161SBR yalnız küçük fokusla (Asım, 29.10.2026) */
}

static uint32_t rec_crc(const nv_record_t *r)
{
    return nv_crc32(r, offsetof(nv_record_t, crc));
}

/* Sürüm 1 kaydını sürüm 2'ye çevirir; yeni alanlar varsayılan değer alır.
 * Kalibrasyon korunur. Bir sonraki nv_save sürüm 2 yazar. */
static bool read_slot_v1(const nv_ops_t *ops, uint32_t addr, nv_record_t *r)
{
    static nv_record_v1_t o;
    if (!ops->read(addr, &o, sizeof o)) return false;
    if (o.h.magic != NV_MAGIC || o.h.version != 1u || o.h.length != sizeof(nv_data_v1_t)) return false;
    if (nv_crc32(&o, offsetof(nv_record_v1_t, crc)) != o.crc) return false;
    if (!calib_valid(&o.d.calib)) return false;
    nv_defaults(&r->d);
    r->h = o.h;
    r->d.calib = o.d.calib;
    r->d.settings.gen_power_max_w = o.d.settings.gen_power_max_w;
    r->d.settings.language = o.d.settings.language;
    r->d.settings.detector_profile = o.d.settings.detector_profile;
    r->d.settings.fluoro_enabled = o.d.settings.fluoro_enabled;
    r->d.settings.pc_required = o.d.settings.pc_required;
    r->d.settings.exposure_count = o.d.settings.exposure_count;
    return true;
}

static bool read_slot(const nv_ops_t *ops, uint32_t addr, nv_record_t *r)
{
    if (!ops->read(addr, r, sizeof(*r))) return false;
    if (r->h.magic == NV_MAGIC && r->h.version == 1u) return read_slot_v1(ops, addr, r);
    if (r->h.magic != NV_MAGIC || r->h.version != NV_VERSION) return false;
    if (r->h.length != sizeof(nv_data_t)) return false;
    if (rec_crc(r) != r->crc) return false;
    if (!calib_valid(&r->d.calib)) return false;   /* CRC doğru ama mantıksız tablo */
    return true;
}

nv_load_result_t nv_load(nvstore_t *nv, const nv_ops_t *ops, nv_data_t *out)
{
    static nv_record_t a, b;   /* yığını korumak için statik */
    nv->ops = ops;
    const bool va = read_slot(ops, NV_SLOT_A_ADDR, &a);
    const bool vb = read_slot(ops, NV_SLOT_B_ADDR, &b);

    if (!va && !vb) {
        nv_defaults(out);
        nv->seq = 0;
        nv->last_slot = 1;   /* ilk kayıt A'ya gider */
        return NV_LOAD_DEFAULTS;
    }
    /* Sıra numarası taşmasına karşı farkla karşılaştır */
    const bool pick_a = va && (!vb || (int32_t)(a.h.seq - b.h.seq) > 0);
    const nv_record_t *r = pick_a ? &a : &b;
    *out = r->d;
    nv->seq = r->h.seq;
    nv->last_slot = pick_a ? 0 : 1;
    return (va && vb) ? NV_LOAD_OK : NV_LOAD_ONE_BAD;
}

bool nv_save(nvstore_t *nv, const nv_data_t *d)
{
    static nv_record_t r;
    if (!calib_valid(&d->calib)) return false;   /* geçersiz tablo hiç yazılmaz */
    memset(&r, 0, sizeof(r));
    r.h.magic = NV_MAGIC;
    r.h.version = NV_VERSION;
    r.h.length = (uint16_t)sizeof(nv_data_t);
    r.h.seq = nv->seq + 1u;
    r.d = *d;
    r.crc = rec_crc(&r);

    const uint8_t slot = (uint8_t)(nv->last_slot ^ 1u);   /* eski kopyanın üzerine */
    const uint32_t addr = slot ? NV_SLOT_B_ADDR : NV_SLOT_A_ADDR;
    if (!nv->ops->write(addr, &r, sizeof(r))) return false;

    /* Geri okuyarak doğrula */
    static nv_record_t chk;
    if (!read_slot(nv->ops, addr, &chk) || chk.h.seq != r.h.seq) return false;

    nv->seq = r.h.seq;
    nv->last_slot = slot;
    return true;
}
