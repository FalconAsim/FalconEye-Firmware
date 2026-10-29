# FalconEye Kumanda Paneli – Firmware

STM32G474 tabanlı kumanda kartının yazılımı. Bu sürüm (0.8) donanımdan
bağımsız **çekirdeği** içerir: ışınlama durum makinesi, tüp koruması, kalibrasyon,
kalıcı ayar saklama, dedektör profilleri, jeneratör protokolü ve yedek zamanlayıcı.
Çekirdek bilgisayarda bir simülatörle test edilir; kart gelmeden mantık
doğrulanır.

## İçerik

| Klasör | Ne var |
|---|---|
| `core/include/fe_config.h` | Sistem sabitleri: ölçekler, süreler, toleranslar |
| `core/src/tube.c` | Canon DF-161SBR tüp koruması: yük eğrisi, anot ısı modeli, görev döngüsü |
| `core/src/exposure_sm.c` | Işınlama durum makinesi |
| `core/src/error_codes.c` | Hata kodları tablosu (tek kaynak: ekran, kayıt, kullanım kılavuzu) |
| `core/src/calib.c` | Çok noktalı kV/mA kalibrasyonu, DAC/ADC dönüşümü |
| `core/src/nvstore.c` | FRAM'de çift kopya + CRC32 kalıcı saklama |
| `core/src/detector.c` | Dedektör profilleri (kapalı/CR, AED, senkron) ve J19 eşlemesi |
| `core/src/hs_line.c` | Dirençle denetlenen el butonu hattı (2 telli kademe) |
| `core/src/proto.c` | Teknogem jeneratör protokolü TGP v1 (Ethernet ve RS-232) |
| `core/src/service.c` | Servis menüsü: floroskopi ve DAP metre opsiyonu, kurulu insert, PIN |
| `backup/backup_timer.c` | Yedek zamanlayıcı (ayrı STM32G031 firmware'inin mantığı) |
| `docs/generator_protocol_EN.md` | Protokol dokümanı (DR yazılımı üreticileri için, İngilizce) |
| `tools/gen_error_table.c` | Kılavuzun hata kodu tablosunu firmware'den üretir |
| `sim/` | PC simülatörü: jeneratör, flaman ve operatörü taklit eder |
| `tests/` | 9 test programı, 1.600'den fazla kontrol |
| `cmake/arm-none-eabi.cmake` | STM32 için çapraz derleme ayarı |

## Durum makinesi

```
BEKLE --1. kademe--> HAZIRLIK --4 s + Filaman tamam (+ dedektör hazır)--> HAZIR
HAZIR --2. kademe--> IŞINLAMA --süre doldu / bırakıldı--> KİLİT
KİLİT --butonlar tamamen bırakıldı--> BEKLE
Hazır olmadan 2. kademe --> KİLİT (tam bırakma şart)
Jeneratör alarmı, kV/mA tolerans, yedek zamanlayıcı, acil stop --> HATA (onay gerekir)
```

- RL5 seri kontağı X-RAY ON'dan 15 ms önce kapanır, X-RAY ON kesildikten 20 ms sonra açılır.
- Işınlama sırasında ölçülen mA'dan gerçek mAs hesaplanır ve kayda yazılır.
- Hazırlık 30 s'den uzun tutulursa flamanı korumak için biter (değer değişikliği ısınmayı yeniden başlatsa da toplam süre sayılır).

### Hazırlıkta değer değişikliği (arayüz tanımı rev. 0.4 §4)

- HAZIRLIK / HAZIR'da kV, mA ve süre değişebilir: hazır lambası söner, filaman ısınması (4 s) baştan başlar. O anda 2. kademe basılıysa "hazır olmadan 2. kademe" (E101) kuralı çekimi engeller.
- Fokus ve mod (radyografi / floroskopi) hazırlıkta değişmez (E213); fokus rölesi yük altında anahtarlanmaz.
- Işınlamada değerler donar.
- Servis sayacı: `warmup_count` (filaman ısınma sayısı).

### PC / Manuel kontrol modu (arayüz tanımı rev. 0.4 §1.1, §5)

- `sm_set_pc_link(sm, required, serial)`: PC modu = bağlantı zorunlu. Yalnız BEKLE / KİLİT'te değişir.
- PC modunda bağlantı yokken hazırlık başlamaz (E701); hazırlıkta koparsa hazırlık iptal (KİLİT); **ışınlamada koparsa ışınlama aynı adımda kesilir** (E702 Ethernet / E703 RS-232, onay gerekir). Kayıtta `cut = true`, TGP'de `CUT=1`.
- Manuel modda bağlantının çekime etkisi yoktur.

## Takılı buton ve kablo kısa devresi

- **El butonu iki kanallı okunur:** her kademede NO ve NC kontak. "Basılı" sayılması için NO kapalı VE NC açık olmalı. Kanallardan biri bile "bırakıldı" derse ışınlama anında durur.
- **Kısa devre veya kopuk kablo:** NO ve NC 20 ms'den uzun tutarsız kalırsa "Buton/pedal kablo arızası" hatası; onay ve tamir gerekir.
- **Pedal:** iki bağımsız kanal (A ve B); aynı kural.
- **Takılı buton:** cihaz açılırken basılı buton veya ışınlamadan sonra 10 s bırakılmayan buton "Buton takılı" hatası verir. Takılı buton ışınlamayı uzatamaz: süre dolunca kesilir ve tam bırakılmadan yeni çekim olmaz.
- Donanım tarafında: yedek zamanlayıcı ve RL5 seri kontağı yazılımdan bağımsız olarak keser.
- **Mevcut el butonu (4 tel, NC yok):** siyah–beyaz 1. kademe (flaman hazırlığı), kırmızı–yeşil 2. kademe (X-RAY ON). Her kademede butonun İÇİNE kontağa seri 1 kΩ ve paralel 3,3 kΩ direnç konur; kart hattı 2,2 kΩ pull-up ile ADC'den okur (`core/src/hs_line.c`). Basılı / bırakılmış / kısa devre / kopuk ayrı ayrı görülür. Kısa devre ve kopukluk NO/NC tutarsızlığına eşlenir: ışınlama aynı adımda durur, 20 ms sonra E402.
- Dirençler konnektör tarafına değil, butonun içine kontağın yanına konmalı; yoksa kablo kısa devresi görülmez.
- NO + NC kontaklı (5–6 telli) butonla da çalışır; o durumda J3 dijital çift kanal olarak okunur.

## Acil stop

- Ayrı konnektör (J22), **iki kanallı**: mantar başlı butonun iki NC kontağı. Panel yanındaki ve dışarıdaki (oda kapısı vb.) butonlar aynı zincire seri bağlanır; harici buton yoksa sokete köprü takılır.
- Kanal 1 donanımda X-RAY ON (PhotoMOS) ve RL5 hattını doğrudan keser; yazılım çalışmasa da ışınlama durur.
- Kanal 2 optokuplörle okunur. Kanallardan **biri** açılsa yeter: ışınlama ve PreHeat aynı ms içinde kesilir, HATA.
- Acil stop geri çevrilince sistem kendiliğinden çalışmaz; ekrandan **elle reset** gerekir.
- İki kanal 100 ms'den uzun farklı kalırsa "Acil stop kablo/kontak arızası".
- Not: bir kanalın kısa devre olması ancak butona basıldığında fark edilir. Kullanım kılavuzunda periyodik acil stop testi tanımlanmalı.

## J2 kontrol hatları (PreHeat, X-RAY ON)

- Jeneratörün opto girişleri anot tarafından beslenir; kart katodu GND'ye çekerek sürer (PhotoMOS + RL5 + acil stop kanal 1 seri).
- **Geri okuma:** her iki hattın durumu okunur, bir önceki adımın komutuyla karşılaştırılır. 5 ms'den uzun uyuşmazlık = "X-RAY ON / PreHeat hattı arızası" (GND'ye kısa devre, yapışık PhotoMOS, kopuk kablo).
- **Inter-Lock ikinci kanal:** panel J2 Inter-Lock'u yalnızca hazırlık ve ışınlamada kapatır. Beklemede X-RAY ON hattı GND'ye kısa devre olursa jeneratör ışınlamaz ve panel hatayı bildirir.
- Kalan risk: hazırlık sırasında (Inter-Lock kapalıyken) oluşan kısa devre, algılanana kadar en fazla ~5 ms ışınlamaya yol açabilir. Kablolama önlemleri (ekranlı, kısa, korumalı kablo) bu yüzden gerekli.
- Doğrulanacak: jeneratörün yalnızca X-RAY ON ve Inter-Lock birlikte aktifken ışınladığı.

## Işınlama doğruluğu kontrolleri

- **kV feedback:** ölçülen kV ayarın %85'inin altına düşerse E302, %110'unun üzerine çıkarsa E306 (20 ms yükselme süresinden sonra, 50 ms süreyle).
- **Işınlama süresi:** kV feedback'in ayarın %75'inin üzerinde kaldığı süre ölçülür ve kayda yazılır. Ayarlanan süreden ±(%10 + 1 ms) fazla saparsa E307. Eşik ve tolerans IEC 60601-2-54'e göre doğrulanacak.
- **Buton erken bırakıldı:** radyografide süre dolmadan bırakılırsa E106; ekranda ayarlanan ve gerçekleşen süre gösterilir. Floroskopide pedalı bırakmak normal bitiştir.

## Kalibrasyon ve kalıcı saklama

- Kalibrasyonsuz cihaz **eski panelden ölçülen** referans tablosuyla çalışır (29.10.2026, DF-161SBR küçük fokus): EP-REF 40–100 kV, V = 0,0529·kV − 0,26; IP-REF 60 kV'de V = 0,487·mA − 0,14, kV satırları 40/60/70/100 (eski panelin uzay yükü düzeltmesi: 100 kV'de 40 kV'nin %78'i). Feedback ölçeği hâlâ nominal (6 V = 110 kV, 6 V = 50 mA). kVp metre ve mAs metre ölçümleri girildikçe çok noktalı tablolar devreye girer (en fazla 16 nokta; mA tablosu 4 kV satırına kadar kV'ye bağlı).
- DAC: 3.0 V referans × 2 kazanç = 6.0 V tavan. NaN veya aralık dışı değer 0 V'a çekilir.
- FRAM'de iki kopya, sıra numarası ve CRC32. Yazma sırasında güç kesilirse diğer kopya kullanılır; geçersiz kalibrasyon tablosu hiç yazılmaz.

## Dedektör profilleri

- Kapalı (CR/film), AED, AED + DR hazır bilgisi (PZ 3025ZF), senkron 2 hat, senkron 3+3 hat.
- Senkron: hazırlık isteği → dedektör hazır → ışınlama isteği → izin → ışınlama. İzin 1 s içinde gelmezse E501; pencere süre dolmadan kapanırsa E502; dedektör hata sinyali E503.
- Her hattın işlevi ve polaritesi profilde tanımlı; PZ 4343Z profili senkron dokümanı gelince eklenecek.

## Jeneratör protokolü (TGP v1, arayüz tanımı rev. 0.4)

- Ayrıntı: `docs/generator_protocol_EN.md`. Satır biçimi `SEQ KOMUT ANAHTAR=DEĞER*CRC16` (CRC-16/CCITT-FALSE).
- Komutlar: `PING`, `HELLO`, `VERSION?`, `SET`, `MODE`, `STATUS?`, `LIMITS?`, `LAST?`, `DETREADY`. Cevaplar `ACK` / `NAK <seq> <kod>` / `PONG`; olaylar `ACTUAL`, `STATE`, `ERROR`, `LIMITS`.
- Protokolde ışınlama başlatan veya hata onaylayan komut yoktur (E905).
- PC / Manuel kontrol modu: Manuel modda ya da PC bağlantısı TGP değilken değer yazılamaz (E907).
- Aynı SEQ ikinci kez gelirse komut yeniden uygulanmaz, önceki cevap tekrarlanır.
- Canlılık: boşta 3 s, ışınlamada 750 ms; link / TCP kopması anında. Bağlantı kopunca DR'nin "dedektör hazır" bilgisi silinir.

## Yedek zamanlayıcı (STM32G031)

- ENABLE çıkışı varsayılan kapalı; ana MCU HAZIR durumunda ARM mesajı gönderir (en az 500 ms'de bir yeniler, 1 s'de geçersiz olur).
- Sınır: ayarlanan süre × 1.1 + 10 ms; radyografide mutlak 10 s, floroskopide 20 s.
- Sınır aşılırsa ENABLE düşer ve kilitlenir. Her çekim yeni ARM ister; silahsızken X-RAY ON hattı aktif görülürse (AND kapısı arızası) kilitlenir.
- UART çerçevesi 10 bayt, CRC8.

## Donanım katmanı (şematikten sonra) için entegrasyon notları

1. 1 ms zamanlayıcı kesmesinde: girişleri oku (debounce) → `det_map_inputs` → `sm_step` → `det_map_outputs` → çıkışları ve DAC'ı yaz (`calib_kv_ref_volts` → `calib_volts_to_dac`).
   - `sm_inputs_t.pc_link_ok`: PC bağlantısı TGP ise `tgp_ctx_t.link_up`, Current ise RS-232 canlılığı. Her 1 ms'de `tgp_tick`; Ethernet PHY link düşünce veya TCP kapanınca `tgp_link_lost`.
   - Panel menüsünde mod değişince hem `sm_set_pc_link` hem `tgp_ctx_t.ctrl` / `link_sel` güncellenir (yalnız BEKLE'de).
   - `sm_inputs_t.focus_fb_small`: fokus rölesi geri bildirimi. `kv_ref_fb` / `ma_ref_fb`: DAC çıkışlarının ADC geri okuması, kalibrasyonun tersiyle kV / mA'ya çevrilmiş. `sm_outputs_t.focus_small` her adımda röle sürücüsüne yazılır.
2. HAZIR durumunda yedek zamanlayıcıya ARM gönder; `backup_ok` girişi yedek zamanlayıcının ENABLE geri okumasıdır.
3. Durum değişince ve her çekimden sonra `tgp_fmt_event_*` ile olay satırı gönder.
4. Açılışta `nv_load`; NV_LOAD_DEFAULTS ise ekranda "Kalibrasyon yok" uyarısı göster.

## Hata kodları

- Her hata bir kodla gösterilir: E1xx kullanım ve oda güvenliği, E2xx tüp koruması, E3xx jeneratör, E4xx donanım/kablo (servis), E5xx dedektör, W6xx uyarı.
- Ekranda kod, kısa mesaj ve "Yapılacak" metni birlikte görünür; aynı kod ekspozisyon kaydına ve DR yazılımına gider.
- Kullanım kılavuzu tablosu firmware'den üretilir: `./build/gen_error_table > hata_kodlari.md`. Kod değişince kılavuz da aynı komutla güncellenir.

## Tüp koruması – insert profilleri

Kurulu insert servis menüsünden seçilir (`tube_select`); `LIMITS?` ve `VERSION?` cevapları buna göre değişir.

| Insert | Fokus | kV | En yüksek mA | Anot | Görev çevrimi | Floroskopi |
|---|---|---|---|---|---|---|
| Canon DF-161SBR (varsayılan) | 1,6 / 0,5 | 40–125 (jeneratör sınırı **100**) | 60 / 15 | 35,5 kJ, 600 W | 1:40 / 1:20 | Küçük fokus, 40–60 kV, 600 W, 20 s |
| Canon D-067SB | 0,6 | 50–100 | 22 | 35 kJ, 250 W | 1:60 | Yok |
| Canon D-023 | 0,2 | 50–100 | 4,3 | 35 kJ, 250 W | 1:60 | Yok |
| CEI OX/105-4 | 2,6 | 50–110 | 50 (jeneratör) | 30 kJ, 250 W | 1:40 (varsayım) | Yok |

- Yük eğrileri veri sayfası grafiklerinden gözle okundu (DC / sabit potansiyel eğrileri), %10 güvenlik payı uygulanır; seri üretimden önce sayısallaştırılacak.
- Grafiği 1 s'den başlayan insertlerde 0,1 s değeri 1 s değerine eşit alındı; D-067SB ve D-023'te radyografi 10 s ile sınırlı (veri sayfası 20 / 40 s).
- Tek fokuslu insertte küçük fokus reddedilir (E214). kV alt sınırı insertten gelir (E201).
- Anot ısı modeli: birinci dereceden, zaman sabiti = ısı kapasitesi / ısı atma hızı.

## Fokus rölesi (yeni kartta)

- **Açılış değerleri:** radyografi modu (floroskopi değil), çift fokuslu insertte **küçük fokus** (60 kV, 10 mA, 100 ms); tek fokuslu insertte tek fokus. Insert seçildikten sonra `sm_load_boot_defaults`.
- Röle **bırakık = büyük fokus**, çekili = küçük fokus (hataya güvenli: röle veya sürücü arızası büyük fokusa düşer; küçük fokus değerleri büyük fokusta zararsızdır). Açılışta küçük fokus için röle çekilir ve 50 ms oturma beklenir.
- Komut yalnız filaman sürülmüyorken değişir; değişimden sonra 50 ms oturma beklenir, bu sürede PreHeat verilmez (yük altında anahtarlama yok).
- Geri bildirim (`focus_relay_readback`): röle kontağı veya zorlamalı kılavuzlu röle. Oturduktan sonra 100 ms uyuşmazlık → **E406** (servis); ışınlamadaysa ışınlama kesilir.

## kV / mA referans geri okuması

- `ref_readback`: DAC çıkışı ikinci bir ADC kanalıyla okunur, bir önceki adımın komutuyla karşılaştırılır (tolerans en az 1 kV / 0,5 mA veya %3). 20 ms uyuşmazlık → **E407** (servis); ışınlamadaysa kesilir.
- Beklemede referans 0 olmalıdır: yüksekte takılı DAC çekimden önce yakalanır.

## Kalibrasyon yok uyarısı

- `nv_load` varsayılan döndüyse ekranda **W602** (`fe_warning_no_calibration`).

## Servis menüsü ve opsiyonlar (Asım, 26.10.2026; arayüz tanımı rev. 0.5 §10)

- **Floroskopi** (bir sonraki dedektörle; opsiyon): sistemde hazır, servis menüsünden açılır/kapanır (`svc_set_fluoro`). Açıkken panelde **tek tuşla** RAD ↔ FLUORO (`sm_toggle_mode`); her mod kendi son değerlerini hatırlar, floroskopiye ilk geçiş küçük fokus, 50 kV, 2 mA, 20 s. Tek fokuslu insertte (D-067SB, D-023, OX/105-4) opsiyon açık olsa da floroskopi kullanılamaz. Kapatılırken floroskopideyse radyografiye döner.
- **DAP metre**: servis menüsünden açılır/kapanır (`svc_set_dap_meter`). Açıkken HAL metre okumasını `sm_record_dap` ile kayda yazar, kart `DONE`'dan sonra `DOSE` satırı gönderir; okuma gelmezse **W603**. **Veteriner versiyonunda kapalı**: kart DAP göndermez, DAP'ı FalconXray hesaplar. Metre arayüzü (muhtemelen J14 RS-485) seçilecek.
- **Insert seçimi**: servis menüsünden (`svc_set_insert`); açılış değerleri yeni insertin sınırlarına göre yüklenir. **Isı modeli sıfırlanmaz**: insert değiştirmek sıcak tüpün ısı kaydını silmenin yolu olmasın.
- Servis moduna yalnız panelden PIN ile girilir (TGP'den girilemez); 3 yanlış PIN → 60 s kilit; 10 dk işlemsiz kalırsa kapanır. Ayar yalnız hazırlık / ışınlama dışında değişir; FRAM'e yazılıp geri okunur, yazılamazsa geri alınır.
- **Büyük fokus** (çift fokuslu insertte): servis menüsünden açılır/kapanır (`svc_set_large_focus`); **varsayılan kapalı** — cihaz DF-161SBR ile yalnız küçük fokusta çalışıyor (Asım, 29.10.2026). Kapalıyken `LIMITS`'te `FO=L` satırı yok, `SET FO=L` → `NAK E906`.
- **kV üst sınırı 100** (`FE_GEN_KV_MAX`): eski panel 100 kV'de sınırlıydı; 100 kV üstü kVp metre doğrulaması ve üretici onayıyla açılır.
- FRAM kayıt sürümü 2 (insert, DAP metre, büyük fokus). Sürüm 1 (v0.8) kaydı kalibrasyon korunarak okunur ve dönüştürülür.
- Açık: servis PIN'inin kaynağı (cihaza özel, seri numarasından türetme önerilir).
