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
| `backup/backup_timer.c` | Yedek zamanlayıcı (ayrı STM32G031 firmware'inin mantığı) |
| `docs/generator_protocol_EN.md` | Protokol dokümanı (DR yazılımı üreticileri için, İngilizce) |
| `tools/gen_error_table.c` | Kılavuzun hata kodu tablosunu firmware'den üretir |
| `sim/` | PC simülatörü: jeneratör, flaman ve operatörü taklit eder |
| `tests/` | 8 test programı, 1.000'den fazla kontrol |
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

- Kalibrasyonsuz cihaz nominal ölçekle çalışır (6 V = 110 kV, 6 V = 50 mA). kVp metre ve mAs metre ölçümleri girildikçe çok noktalı tablolar devreye girer (en fazla 16 nokta; mA tablosu 4 kV satırına kadar kV'ye bağlı).
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
2. HAZIR durumunda yedek zamanlayıcıya ARM gönder; `backup_ok` girişi yedek zamanlayıcının ENABLE geri okumasıdır.
3. Durum değişince ve her çekimden sonra `tgp_fmt_event_*` ile olay satırı gönder.
4. Açılışta `nv_load`; NV_LOAD_DEFAULTS ise ekranda "Kalibrasyon yok" uyarısı göster.

## Hata kodları

- Her hata bir kodla gösterilir: E1xx kullanım ve oda güvenliği, E2xx tüp koruması, E3xx jeneratör, E4xx donanım/kablo (servis), E5xx dedektör, W6xx uyarı.
- Ekranda kod, kısa mesaj ve "Yapılacak" metni birlikte görünür; aynı kod ekspozisyon kaydına ve DR yazılımına gider.
- Kullanım kılavuzu tablosu firmware'den üretilir: `./build/gen_error_table > hata_kodlari.md`. Kod değişince kılavuz da aynı komutla güncellenir.

## Tüp koruması (DF-161SBR)

- 40–125 kV (jeneratör 110 kV), büyük fokus en fazla 60 mA, küçük fokus en fazla 15 mA.
- Yük eğrisi tablosu kV ve süreye göre ara değer bulur, %10 güvenlik payı uygular.
- Radyografi en fazla 10 s; floroskopi yalnızca küçük fokus, 40–60 kV, en fazla 600 W ve 20 s.
- Anot ısı modeli: 35.5 kJ kapasite, 600 W ısı atma; doluysa ışınlamaya izin vermez.
- Görev döngüsü: büyük fokus 1:40, küçük fokus 1:20 bekleme.
- Jeneratör gücü sınırı: 3.2 kW (`fe_config.h`).

## Bilgisayarda derleme ve test

Gerekenler: CMake ve bir C derleyicisi.

- **Windows:** [STM32CubeCLT](https://www.st.com/en/development-tools/stm32cubeclt.html) (CMake, Ninja ve ARM derleyicisini birlikte kurar) + bilgisayar testleri için MinGW-w64 veya Visual Studio Build Tools. Kod düzenleyici olarak VS Code ve "STM32Cube for VS Code" eklentisi.
- **Linux:** `sudo apt install build-essential cmake gcc-arm-none-eabi`

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

STM32 için çekirdeği derlemek:

```
cmake -S . -B build-arm -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
cmake --build build-arm
```

## Doğrulanması gereken varsayımlar

1. **Yük eğrisi değerleri** datasheet grafiğinden gözle okundu. Seri üretimden önce eğriler sayısallaştırılmalı.
2. **Anot ısı modeli** birinci dereceden; datasheet'teki ısınma/soğuma eğrisi ve ölçümle ince ayar yapılacak.
3. **Floroskopi pedalı:** pedal basılınca hazırlık başlar, hazır olunca pedal basılı tutuldukça otomatik ışınlar. Bu akışın klinik kullanıma uygunluğu onaylanmalı.
4. **Toleranslar:** kV ±%10, mA ±%20, 20 ms yükselme süresinden sonra, 50 ms süreyle aşılırsa hata. Kalibrasyon ölçümlerinden sonra sıkılaştırılabilir.

## Sıradaki adımlar

- Kalibrasyon tabloları ve FRAM'de CRC'li saklama
- Dedektör profilleri (AED / senkron / kapalı)
- Teknogem jeneratör protokolü (Ethernet ve RS-232)
- Yedek zamanlayıcı firmware'i (STM32G031)
- Şematikten sonra: pin atamaları, DAC/ADC, BT817 ekran, W5500, Türkçe arayüz

## Mevzuat

Bu yazılım IEC 62304 kapsamındadır. Güvenliğin tek bariyeri değildir:
donanım güvenlik zinciri (yedek zamanlayıcı, watchdog, interlock, aşım
komparatörü) yazılımdan bağımsız olarak ışınlamayı keser.
