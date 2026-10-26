/*
 * Hata kodları tablosu. Metinler UTF-8 Türkçe; ekran yazı tipi Türkçe
 * karakterleri içerir. İngilizce ve Çince çeviriler aynı kod numarasıyla
 * ayrı dil tablolarına eklenecek.
 */
#include "error_codes.h"
#include <stddef.h>

static const fe_error_t table[] = {
    /* --- E1xx Kullanım ve oda güvenliği --- */
    { 101, 'E', SEV_INFO,    "Flaman ısınmadan 2. kademeye basıldı",
      "El butonunun 2. kademesine, 1. kademede flaman ısınıp hazır lambası yanmadan basıldı.",
      "Butonu tamamen bırakın. 1. kademeye basın, hazır lambasını bekleyin, sonra 2. kademeye basın." },
    { 102, 'E', SEV_INFO,    "Hazırlık çok uzun tutuldu",
      "1. kademe 30 saniyeden uzun basılı tutuldu; flamanı korumak için hazırlık durduruldu.",
      "Butonu bırakın ve çekime hazır olduğunuzda yeniden başlayın." },
    { 103, 'E', SEV_INFO,    "Kapı açık",
      "Oda kapısı interlock devresi açık (J21).",
      "Kapıyı kapatın. Kapı kapalıysa kapı anahtarını ve kablosunu kontrol ettirin." },
    { 104, 'E', SEV_ERROR,   "Acil stop basıldı",
      "Acil stop butonlarından biri basıldı (J22).",
      "Tehlike giderildikten sonra acil stop butonunu çevirerek serbest bırakın ve ekrandan onaylayın." },

    { 106, 'E', SEV_INFO,    "Buton erken bırakıldı",
      "Işınlama sırasında el butonu ayarlanan süre dolmadan bırakıldı; çekim eksik dozla bitti. Ekranda ayarlanan ve gerçekleşen süre gösterilir.",
      "Görüntüyü kontrol edin. Gerekirse butonu süre bitene ve ses kesilene kadar basılı tutarak çekimi tekrarlayın." },

    /* --- E2xx Tüp koruması --- */
    { 201, 'E', SEV_INFO,    "kV tüp alt sınırının altında",
      "Seçilen kV, tüpün en düşük çalışma geriliminin (40 kV) altında.",
      "kV değerini 40 kV veya üzerine ayarlayın." },
    { 202, 'E', SEV_INFO,    "kV üst sınırın üzerinde",
      "Seçilen kV, tüpün veya jeneratörün üst sınırını aşıyor.",
      "kV değerini düşürün." },
    { 203, 'E', SEV_INFO,    "mA bu fokus için çok yüksek",
      "Seçilen mA, fokusun izin verilen en yüksek akımını aşıyor (küçük fokus 15 mA, büyük fokus 60 mA).",
      "mA değerini düşürün veya büyük fokusu seçin." },
    { 204, 'E', SEV_INFO,    "Tüp yük sınırı aşılıyor",
      "Seçilen kV, mA ve süre birlikte tüpün yük eğrisini aşıyor.",
      "mA veya süreyi düşürün; gerekirse büyük fokusu seçin." },
    { 205, 'E', SEV_INFO,    "Süre üst sınırın üzerinde",
      "Işınlama süresi sınırı aşıyor (radyografi 10 s, floroskopi 20 s).",
      "Süreyi kısaltın." },
    { 206, 'E', SEV_INFO,    "Jeneratör gücü aşılıyor",
      "kV × mA, jeneratörün en yüksek gücünü aşıyor.",
      "kV veya mA değerini düşürün." },
    { 207, 'E', SEV_INFO,    "Tüp ısındı, bekleyin",
      "Tüpün anot ısısı bu çekim için yeterli değil.",
      "Ekrandaki ısı göstergesi düşene kadar bekleyin veya daha düşük bir çekim değeri seçin." },
    { 208, 'E', SEV_INFO,    "Soğuma beklemesi sürüyor",
      "Tüpün çekimler arası bekleme süresi dolmadı.",
      "Ekrandaki geri sayım bitene kadar bekleyin." },
    { 209, 'E', SEV_INFO,    "Floroskopi yalnızca küçük fokus",
      "Floroskopi modunda büyük fokus seçilemez.",
      "Küçük fokusu seçin." },
    { 210, 'E', SEV_INFO,    "Floroskopi kV aralığı dışında",
      "Floroskopi 40–60 kV aralığında yapılabilir.",
      "kV değerini 40–60 kV aralığına ayarlayın." },
    { 211, 'E', SEV_INFO,    "Floroskopi gücü aşılıyor",
      "Floroskopide kV × mA en fazla 600 W olabilir.",
      "kV veya mA değerini düşürün." },
    { 212, 'E', SEV_INFO,    "Geçersiz ayar",
      "Çekim değerlerinden biri geçersiz (ör. süre veya mA sıfır).",
      "Çekim değerlerini yeniden girin." },
    { 220, 'E', SEV_ERROR,   "Tüp aşırı sıcak",
      "Tüp termik anahtarı açıldı.",
      "Tüpün soğumasını bekleyin. Hata soğuk tüpte sürüyorsa servisi arayın." },

    /* --- E3xx Jeneratör ve ışınlama kalitesi --- */
    { 301, 'E', SEV_ERROR,   "Jeneratör alarmı",
      "Jeneratör bir alarm veya kV/mA aşımı bildirdi.",
      "Ekrandan onaylayın ve yeniden deneyin. Tekrarlarsa servisi arayın." },
    { 302, 'E', SEV_ERROR,   "kV düşük (%85 altında)",
      "Işınlama sırasında ölçülen kV (feedback), ayarlanan değerin %85'inin altına düştü.",
      "Ekrandan onaylayın ve yeniden deneyin. Tekrarlarsa servisi arayın." },
    { 303, 'E', SEV_ERROR,   "mA ayarlanan değerde değil",
      "Işınlama sırasında ölçülen mA, ayarlanan değerden fazla saptı.",
      "Ekrandan onaylayın ve yeniden deneyin. Tekrarlarsa servisi arayın." },
    { 304, 'E', SEV_INFO,    "Flaman hazır olmadı",
      "Hazırlık süresinde \"Filaman tamam\" sinyali gelmedi.",
      "Butonu bırakıp yeniden deneyin. Tekrarlarsa servisi arayın." },
    { 305, 'E', SEV_INFO,    "Flaman sinyali kayboldu",
      "Hazır veya ışınlama sırasında \"Filaman tamam\" sinyali kesildi.",
      "Butonu bırakıp yeniden deneyin. Tekrarlarsa servisi arayın." },

    { 306, 'E', SEV_ERROR,   "kV yüksek (%110 üzerinde)",
      "Işınlama sırasında ölçülen kV (feedback), ayarlanan değerin %110'unu aştı.",
      "Ekrandan onaylayın ve yeniden deneyin. Tekrarlarsa cihazı kullanmayın ve servisi arayın." },
    { 307, 'E', SEV_ERROR,   "Işınlama süresi sapması",
      "Ölçülen ışınlama süresi (kV feedback'ten), ayarlanan süreden ±(%10 + 1 ms) fazla saptı. Ekranda ayarlanan ve ölçülen süre gösterilir.",
      "Ekrandan onaylayın ve yeniden deneyin. Tekrarlarsa servisi arayın." },

    /* --- E4xx Donanım ve kablo arızası --- */
    { 401, 'E', SEV_SERVICE, "Yedek zamanlayıcı ışınlamayı kesti",
      "Bağımsız güvenlik zamanlayıcısı ışınlamayı sonlandırdı.",
      "Cihazı kullanmayın ve servisi arayın." },
    { 402, 'E', SEV_SERVICE, "Buton veya pedal takılı",
      "El butonu veya ayak pedalı açılışta basılı ya da çekimden sonra bırakılmadı.",
      "Butonu ve pedalı serbest bırakın. Hata sürüyorsa butonu değiştirtin." },
    { 403, 'E', SEV_SERVICE, "Buton/pedal kablo arızası",
      "El butonu veya pedalın iki kanalı farklı bilgi veriyor (kısa devre veya kopuk kablo).",
      "Cihazı kullanmayın. Buton/pedal kablosunu servise kontrol ettirin." },
    { 404, 'E', SEV_SERVICE, "Acil stop kablo arızası",
      "Acil stop devresinin iki kanalı farklı bilgi veriyor.",
      "Cihazı kullanmayın. Acil stop devresini servise kontrol ettirin." },
    { 405, 'E', SEV_SERVICE, "X-RAY ON / PreHeat hattı arızası",
      "Jeneratöre giden kontrol hattının durumu panelin komutuyla uyuşmuyor (kısa devre, kopukluk veya anahtar arızası).",
      "Cihazı kullanmayın ve servisi arayın." },

    /* --- E5xx Dedektör ve haberleşme --- */
    { 501, 'E', SEV_INFO,    "Dedektör hazır değil",
      "Dedektör hazır bilgisi zamanında gelmedi.",
      "DR yazılımında dedektörün hazır olduğunu kontrol edin ve yeniden deneyin." },

    { 502, 'E', SEV_ERROR,   "Dedektör ışınlamayı erken sonlandırdı",
      "Senkron çalışan dedektörün ışınlama izni, ayarlanan süre dolmadan kapandı.",
      "Dedektör ayarlarını (pencere süresi) ve ışınlama süresini kontrol edin. Tekrarlarsa servisi arayın." },
    { 503, 'E', SEV_ERROR,   "Dedektör hatası",
      "Dedektör senkron hattından hata bildirdi.",
      "DR yazılımında dedektörün durumunu kontrol edin, ekrandan onaylayın ve yeniden deneyin." },

    /* --- W6xx Uyarılar --- */
    { 601, 'W', SEV_WARNING, "Floroskopi 5 dakikaya ulaştı",
      "Toplam floroskopi süresi 5 dakikaya ulaştı (IEC 60601-2-54).",
      "Uyarıyı ekrandan onaylayın; sayaç sıfırlanır." },
};

#define N_ERR (sizeof(table) / sizeof(table[0]))

static const fe_error_t *by_code(uint16_t code)
{
    for (size_t i = 0; i < N_ERR; i++) {
        if (table[i].code == code) return &table[i];
    }
    return NULL;
}

static uint16_t tube_code(tube_result_t t)
{
    switch (t) {
    case TUBE_ERR_KV_LOW:       return 201;
    case TUBE_ERR_KV_HIGH:      return 202;
    case TUBE_ERR_MA_FOCUS_MAX: return 203;
    case TUBE_ERR_RATING:       return 204;
    case TUBE_ERR_TIME_MAX:     return 205;
    case TUBE_ERR_GEN_POWER:    return 206;
    case TUBE_ERR_HEAT:         return 207;
    case TUBE_ERR_COOLDOWN:     return 208;
    case TUBE_ERR_FLUORO_FOCUS: return 209;
    case TUBE_ERR_FLUORO_KV:    return 210;
    case TUBE_ERR_FLUORO_POWER: return 211;
    case TUBE_ERR_PARAM:        return 212;
    case TUBE_OK:               return 0;
    }
    return 212;
}

const fe_error_t *fe_error_for(end_reason_t r, tube_result_t t)
{
    switch (r) {
    case END_NONE:
    case END_TIME_DONE:
    case END_RELEASED:         return NULL;
    case END_EARLY_STAGE2:     return by_code(101);
    case END_PREP_TIMEOUT:     return by_code(102);
    case END_INTERLOCK:        return by_code(103);
    case END_ESTOP:            return by_code(104);
    case END_TUBE_REJECT:      return by_code(tube_code(t));
    case END_TUBE_THERMAL:     return by_code(220);
    case END_GEN_ALARM:        return by_code(301);
    case END_KV_LOW:           return by_code(302);
    case END_KV_HIGH:          return by_code(306);
    case END_EARLY_RELEASE:    return by_code(106);
    case END_TIME_DEV:         return by_code(307);
    case END_DETECTOR_WINDOW:  return by_code(502);
    case END_DETECTOR_ERROR:   return by_code(503);
    case END__COUNT:           return NULL;
    case END_MA_TOL:           return by_code(303);
    case END_FILAMENT_TIMEOUT: return by_code(304);
    case END_FILAMENT_LOST:    return by_code(305);
    case END_BACKUP_TIMER:     return by_code(401);
    case END_BUTTON_STUCK:     return by_code(402);
    case END_INPUT_CHANNEL:    return by_code(403);
    case END_ESTOP_CHANNEL:    return by_code(404);
    case END_LINE_FAULT:       return by_code(405);
    case END_DETECTOR_TIMEOUT: return by_code(501);
    }
    return NULL;
}

const fe_error_t *fe_warning_fluoro_cumulative(void) { return by_code(601); }

const fe_error_t *fe_error_table(uint32_t *count)
{
    if (count) *count = (uint32_t)N_ERR;
    return table;
}

const char *fe_severity_text(fe_severity_t s)
{
    switch (s) {
    case SEV_INFO:    return "Bilgi";
    case SEV_WARNING: return "Uyarı";
    case SEV_ERROR:   return "Hata (onay gerekir)";
    case SEV_SERVICE: return "Servis";
    }
    return "?";
}
