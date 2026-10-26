/*
 * Dedektör profilleri ve J19 senkron portu
 *
 * J19: 3 izole giriş (5–24 V) + 3 PhotoMOS çıkış. Hangi hattın ne işe
 * yaradığı ve polaritesi dedektör üreticisine göre değişir; bu yüzden
 * servis menüsünden seçilen bir profil ile tanımlanır.
 *
 * Profil türleri:
 *   KAPALI  : CR kaset/film veya dedektörsüz kullanım. Hazır izni aranmaz.
 *   AED     : dedektör ışınlamayı kendisi algılar. İsteğe bağlı olarak
 *             "dedektör hazır" bilgisi DR yazılımından (Ethernet) alınır.
 *   SENKRON : J19 hatlarıyla el sıkışma: hazırlık isteği -> dedektör hazır
 *             -> ışınlama isteği -> ışınlama izni -> ışınlama.
 */
#ifndef DETECTOR_H
#define DETECTOR_H

#include <stdbool.h>
#include <stdint.h>
#include "exposure_sm.h"
#include "nvstore.h"

#define DET_LINES 3

typedef enum {
    DET_IN_NONE = 0,
    DET_IN_READY,       /* dedektör hazır (hazırlık sonrası) */
    DET_IN_ENABLE,      /* ışınlama izni / entegrasyon penceresi açık */
    DET_IN_ERROR        /* dedektör hatası */
} det_in_func_t;

typedef enum {
    DET_OUT_NONE = 0,
    DET_OUT_PREP,       /* hazırlık isteği (1. kademe) */
    DET_OUT_EXP_REQ,    /* ışınlama isteği (2. kademe) */
    DET_OUT_XRAY_ON     /* ışınlama sürüyor */
} det_out_func_t;

typedef struct {
    const char *name;
    det_profile_kind_t kind;
    bool ready_from_pc;              /* AED: hazır bilgisi DR yazılımından */
    det_in_func_t in_func[DET_LINES];
    bool in_active_low[DET_LINES];   /* true: giriş gerilimsizken aktif */
    det_out_func_t out_func[DET_LINES];
    bool out_active_open[DET_LINES]; /* true: aktifken kontak açık */
} det_profile_t;

/* Profil doğrulaması: SENKRON'da en az ENABLE girişi ve EXP_REQ çıkışı olmalı;
 * aynı işlev iki hatta atanamaz. */
bool det_profile_valid(const det_profile_t *p);

/* Profilin durum makinesi ayarlarına etkisi */
void det_apply_config(const det_profile_t *p, sm_config_t *cfg);

/* Ham J19 girişleri (+ DR yazılımından gelen hazır bilgisi) -> mantıksal sinyaller */
void det_map_inputs(const det_profile_t *p, const bool raw_in[DET_LINES], bool pc_ready,
                    sm_inputs_t *in);

/* Durum makinesi çıkışları -> ham J19 PhotoMOS komutları */
void det_map_outputs(const det_profile_t *p, const sm_outputs_t *out, bool raw_out[DET_LINES]);

/* Hazır profiller */
const det_profile_t *det_builtin(uint32_t index);
uint32_t det_builtin_count(void);

#endif
