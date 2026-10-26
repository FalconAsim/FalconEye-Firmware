/*
 * Kalıcı ayar saklama (FRAM MB85RC256V, 32 KB)
 *
 * İki kopya (A/B) + sıra numarası + CRC32. Her yazma eski kopyanın üzerine
 * yapılır; yazma sırasında güç kesilse bile diğer kopya sağlam kalır.
 * Okumada geçerli kopyalardan sıra numarası büyük olan seçilir. İkisi de
 * bozuksa varsayılan değerler yüklenir ve bu durum raporlanır (cihaz
 * kalibrasyonsuz çalıştığını ekranda belirtmeli).
 *
 * Erişim, HAL'in sağladığı okuma/yazma fonksiyonlarıyla yapılır; böylece
 * aynı kod PC testlerinde bellek dizisiyle çalışır.
 */
#ifndef NVSTORE_H
#define NVSTORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "calib.h"

#define NV_MAGIC        0x4C434546u   /* "FECL" */
#define NV_VERSION      2u   /* 2: insert ve DAP metre ayarı eklendi; 1 okunur ve dönüştürülür */
#define NV_SLOT_SIZE    2048u
#define NV_SLOT_A_ADDR  0x0000u
#define NV_SLOT_B_ADDR  0x0800u

typedef enum { DET_PROFILE_OFF = 0, DET_PROFILE_AED, DET_PROFILE_SYNC } det_profile_kind_t;

/* Saha ayarları (servis menüsü) */
typedef struct {
    float gen_power_max_w;      /* jeneratör gücü sınırı */
    uint8_t language;           /* 0 Türkçe, 1 İngilizce, 2 Çince */
    uint8_t detector_profile;   /* det_profile_kind_t */
    uint8_t fluoro_enabled;     /* floroskopi lisansı/opsiyonu */
    uint8_t pc_required;        /* DR bağlantısı yoksa çekime izin verme */
    uint32_t exposure_count;    /* toplam çekim sayacı */
    /* --- sürüm 2 --- */
    uint8_t tube_insert;        /* tube_insert_t: kurulu insert (servis menüsü) */
    uint8_t dap_meter_enabled;  /* DAP metre takılı (insan versiyonu); veterinerde 0 */
    uint8_t reserved[6];
} nv_settings_t;

typedef struct {
    calib_t calib;
    nv_settings_t settings;
} nv_data_t;

typedef struct {
    bool (*read)(uint32_t addr, void *buf, size_t len);
    bool (*write)(uint32_t addr, const void *buf, size_t len);
} nv_ops_t;

typedef enum {
    NV_LOAD_OK = 0,        /* geçerli kopya yüklendi */
    NV_LOAD_ONE_BAD,       /* bir kopya bozuk, diğeri yüklendi (bir sonraki kayıtta onarılır) */
    NV_LOAD_DEFAULTS       /* iki kopya da geçersiz: varsayılanlar */
} nv_load_result_t;

typedef struct {
    const nv_ops_t *ops;
    uint32_t seq;          /* son geçerli sıra numarası */
    uint8_t last_slot;     /* 0 = A, 1 = B */
} nvstore_t;

void nv_defaults(nv_data_t *d);
nv_load_result_t nv_load(nvstore_t *nv, const nv_ops_t *ops, nv_data_t *out);
bool nv_save(nvstore_t *nv, const nv_data_t *d);

uint32_t nv_crc32(const void *data, size_t len);

#endif
