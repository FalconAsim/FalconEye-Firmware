/*
 * FalconEye devreye alma (bring-up) firmware'i – donanımdan bağımsız çekirdek.
 *
 * Kartlar ilk geldiğinde kullanılır: raylar, girişler, çıkışlar (tek tek), DAC,
 * çevre birim kimlikleri, CAN, NTC, fan. Üretim firmware'i DEĞİLDİR.
 *
 * Güvenlik kuralları (değiştirilemez; bu dosyadaki tablo ve bekçi ile uygulanır):
 *  - X-RAY ON isteği (Kart B XRAY_REQ, PE2) devreye alma firmware'inde HİÇ sürülmez;
 *    çıkış tablosunda yoktur. Işınlama zinciri üretim firmware'iyle, kabul testinde denenir.
 *  - Jeneratöre giden hatlar (J2: sofaj, ön ısıtma, interlock, alarm sıfırlama, RL5) ve
 *    invertör röleleri (J4) yalnız "j2onay" komutundan sonra, en çok 2 s açılır.
 *    Operatör onayı: J2 ve J4 fişleri sökülü.
 *  - Bütün çıkışların açık kalma süresi sınırlıdır; süre dolunca bekçi kapatır.
 *  - El butonu, acil stop, kapı ve takılı buton donanım zinciri firmware'den bağımsızdır;
 *    bu firmware zincire dokunmaz, yalnız durumlarını okur.
 *
 * Bu dosya PC'de test edilir (tests/test_bringup.c).
 */
#ifndef BU_CORE_H
#define BU_CORE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---------------- sonuç / sınır ---------------- */
typedef enum { BU_FAIL = 0, BU_PASS = 1, BU_INFO = 2, BU_SKIP = 3 } bu_verdict_t;

typedef struct {
    const char *name;      /* "+24V" */
    float lo, hi;          /* kabul aralığı */
    const char *unit;      /* "V" */
} bu_limit_t;

bu_verdict_t bu_judge(const bu_limit_t *l, float v);
/* "GECTI  +24V            24.12 V   [21.60 .. 26.40]" biçiminde satır; dönen değer yazılan uzunluk */
int bu_fmt_result(char *buf, size_t n, const bu_limit_t *l, float v, bu_verdict_t r);
const char *bu_verdict_text(bu_verdict_t r);

/* ---------------- ölçüm dönüşümleri ---------------- */
/* ADC kodu → bölücü öncesi gerilim. r_top üstte (kaynağa), r_bot altta (GND'ye). */
float bu_adc_to_v(uint16_t code, float vref, uint16_t full, float r_top, float r_bot);
/* VREFINT ile VDDA / VREF+ hesabı (ST: VREF+ = 3,0 V × VREFINT_CAL / VREFINT_DATA) */
float bu_vref_from_vrefint(uint16_t vrefint_cal, uint16_t vrefint_data, float cal_v);
/* İç sıcaklık sensörü (ST iki noktalı kalibrasyon). code ölçüm gerilimi vref'te; kalibrasyon cal_v'de. */
float bu_mcu_temp_c(uint16_t code, float vref, uint16_t ts_cal1, uint16_t ts_cal2,
                    float t1, float t2, float cal_v);

/* NTC bölücüsü: r_pu üstte (VDDA'ya), NTC altta. ratio = Vadc / VDDA (oransal).
 * Dönüş: °C; açık devre (ratio ≥ 0,98) veya kısa devre (≤ 0,02) için NaN. */
float bu_ntc_c(float ratio, float r_pu, float r25, float beta);
bool  bu_is_nan(float x);

/* ---------------- CAN devreye alma çerçeveleri ---------------- */
/* Üretim kimlikleriyle çakışmaz (0x7E0–0x7EF). Klasik CAN, 500 kbit/s, 8 bayt. */
#define BU_CAN_PING      0x7E0u   /* B → A: yankı isteği (8 bayt rastgele / sayaç) */
#define BU_CAN_HB_A      0x7E1u   /* A kalp atışı */
#define BU_CAN_HB_B      0x7E2u   /* B kalp atışı */
#define BU_CAN_HB_M      0x7E3u   /* M kalp atışı */
#define BU_CAN_M_DATA    0x7E4u   /* M ölçümleri: dp Pa×10 i16 · ABP kPa×100 i16 · SDP °C×10 i16 · ADS kimliği u8 · bayrak u8 */
#define BU_CAN_PONG      0x7E8u   /* A → B: yankı (aynı yük) */

/* Kalp atışı yükü */
#define BU_ST_HSE        0x01u    /* harici kristal çalışıyor */
#define BU_ST_RAILS      0x02u    /* raylar sınır içinde */
#define BU_ST_PERIPH     0x04u    /* ana çevre birim (A: BT817, B: W5500, M: ADS1292R) kimliği doğru */
#define BU_ST_PEER1      0x08u    /* A: B görülüyor · M: A görülüyor */
#define BU_ST_PEER2      0x10u    /* A: M görülüyor */
#define BU_ST_SENS       0x20u    /* M: basınç sensörleri yanıt veriyor · A: NTC geçerli */

typedef struct {
    char board;          /* 'A' 'B' 'M' */
    uint8_t seq;
    uint16_t uptime_s;
    uint8_t status;      /* BU_ST_* */
    int8_t temp_c;       /* MCU iç sıcaklığı (−128 = yok) */
    uint16_t aux;        /* A: 24 V (10 mV birimi) · B: 24 V · M: ADS kimliği << 8 | sensör bayrakları */
} bu_hb_t;

void bu_hb_pack(const bu_hb_t *h, uint8_t out[8]);
bool bu_hb_unpack(const uint8_t in[8], bu_hb_t *h);   /* geçersiz kart harfinde false */

/* Eş görülme izleme: son kalp atışından bu yana süre */
typedef struct { uint32_t last_ms; bool ever; bu_hb_t last; } bu_peer_t;
void bu_peer_seen(bu_peer_t *p, const bu_hb_t *h, uint32_t now_ms);
bool bu_peer_alive(const bu_peer_t *p, uint32_t now_ms, uint32_t timeout_ms);

/* ---------------- çıkış bekçisi ---------------- */
typedef enum { BU_GRP_GENEL = 0, BU_GRP_J2J4 } bu_grp_t;

typedef struct {
    const char *name;     /* konsolda yazılan ad */
    const char *desc;
    bu_grp_t grp;
    uint32_t max_on_ms;   /* bekçi süresi (0 = sınırsız; yalnız LED gibi zararsızlar) */
} bu_out_def_t;

#define BU_MAX_OUT 32
#define BU_ACK_VALID_MS (10u * 60u * 1000u)   /* j2onay 10 dk geçerli */
#define BU_J2_MAX_ON_MS 2000u

typedef struct {
    const bu_out_def_t *defs;
    int n;
    bool on[BU_MAX_OUT];
    uint32_t t_on[BU_MAX_OUT];
    bool ack;
    uint32_t ack_ms;
} bu_guard_t;

typedef enum { BU_OK = 0, BU_ERR_NAME, BU_ERR_ACK, BU_ERR_FORBIDDEN } bu_err_t;

void bu_guard_init(bu_guard_t *g, const bu_out_def_t *defs, int n);
int  bu_guard_find(const bu_guard_t *g, const char *name);
/* İstek; izin verilirse g->on güncellenir (donanıma yazmak çağıranın işi) */
bu_err_t bu_guard_set(bu_guard_t *g, int idx, bool on, uint32_t now_ms);
bu_err_t bu_guard_set_name(bu_guard_t *g, const char *name, bool on, uint32_t now_ms);
void bu_guard_ack(bu_guard_t *g, uint32_t now_ms);
bool bu_guard_ack_valid(const bu_guard_t *g, uint32_t now_ms);
/* Süresi dolanları kapatır; kapatılan çıkış sayısını döndürür */
int  bu_guard_tick(bu_guard_t *g, uint32_t now_ms);
void bu_guard_all_off(bu_guard_t *g);
/* Yasak adlar (XRAY_REQ vb.) tabloya hiç girmemeli: tablo doğrulaması (init'te çağrılır) */
bool bu_guard_table_ok(const bu_out_def_t *defs, int n);

/* ---------------- konsol ---------------- */
#define BU_LINE_MAX 64
#define BU_ARGV_MAX 6
typedef struct {
    char line[BU_LINE_MAX];
    int len;
} bu_console_t;

/* Karakter besle; tam satır gelince true döner ve satırı sözcüklere ayırır */
bool bu_console_feed(bu_console_t *c, char ch, int *argc, char *argv[BU_ARGV_MAX]);
/* Ondalık / 0x on altılık tamsayı */
bool bu_parse_int(const char *s, long *out);
/* Küçük harfe çevirerek karşılaştırma */
bool bu_streq(const char *a, const char *b);

/* ---------------- küçük biçimleyici (printf'siz hedef için) ---------------- */
int bu_fmt_f(char *buf, size_t n, float v, int decimals);   /* "-12.34" */
int bu_fmt_hex(char *buf, size_t n, uint32_t v, int digits);

#endif
