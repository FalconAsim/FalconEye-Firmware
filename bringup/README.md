# FalconEye – devreye alma (bring-up) firmware'i

Sürüm 0.1 · 10.11.2026 · TASLAK.

Kartlar ilk geldiğinde kullanılır. Sırasıyla şunları dener:
- raylar, girişler ve çıkışlar (tek tek);
- DAC → J2-5/J2-6 geri okuması;
- çevre birim kimlikleri;
- CAN, NTC ve fan.

**Üretim firmware'i değildir:** ışınlama yapmaz, durum makinesi yoktur.

| Kart | MCU | Dosya | Sonuç nereye yazılır |
|---|---|---|---|
| Kart B | STM32G474VET6 | `bringup_kart_b.hex` | RS-232 konsol: J20 → arka panel P2 DB9 (uç 2 RXD, 3 TXD, 5 GND), 115200 8N1 |
| Kart A | STM32G0B1RET6 | `bringup_kart_a.hex` | Ekran (test deseni + canlı satırlar). İstenirse 3,3 V TTL konsol J42: uç 2 TX, 3 RX, 4 GND (uç 1 +5 V bağlanmaz) |
| Kart M | STM32G0B1CBU6 | `bringup_kart_m.hex` | Konsolu yok. Sonuçlar CAN ile Kart A'nın ekranına ve raporuna gider. LED_M: 1 Hz = tamam, 5 Hz = kalem kaldı, sürekli yanık = CAN başlamadı |

## Güvenlik kuralları (firmware'de sabit)

Bu kurallar değiştirilemez. `bu_core.c` / `bu_boards.c` içindedir ve `tests/test_bringup.c` ile denetlenir.

1. **X-RAY ON isteği (Kart B XRAY_REQ, PE2) hiç sürülmez.** Uç açılışta düşük çıkış yapılır ve öyle kalır. Çıkış tablosunda yer almaz; `cikis xray_req 1` reddedilir. Işınlama zinciri ancak üretim firmware'iyle, kabul testinde denenir.
2. **Jeneratör (J2) ve invertör röle (J4) hatları kilitlidir.** Bu hatlar: sofaj, ön ısıtma, interlock, alarm sıfırlama, RL5, fokus, kondansatör şarjı.
   - Önce J2 ve J4 fişleri sökülür, sonra `j2onay EVET` yazılır. Onay 10 dk geçerlidir.
   - Bu hatlar en çok **2 s** açık kalır. Onay süresi dolunca açık olan hat da kapanır.
3. **Her çıkışın süre sınırı vardır** (genel 5 s, kolimatör 30 s, buzzer 1 s). Süre dolunca bekçi çıkışı kapatır. Fan ve LED sınırsızdır.
4. **DAC yalnız `dac` komutu süresince açıktır** (≈ 50 ms), sonra kapanır. Komut j2onay ister.
5. **Donanım güvenlik zincirine dokunulmaz.** El butonu iki kademe, acil stop, kapı, takılı buton kesmesi ve yedek zamanlayıcı firmware'den bağımsızdır. Bu firmware zinciri yalnız okur.
6. Kart B'nin harici bekçisi TPS3823 (U11) için WDI ucu 100 ms'de bir değiştirilir.

## Derleme

```sh
tools/fetch_cmsis.sh                                    # bir kez: ST + ARM CMSIS başlıkları (sabit sürüm)
cmake -S . -B build-b  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake     -DFE_MCU=G474 -DCMAKE_BUILD_TYPE=Release
cmake --build build-b  --target bringup_kart_b
cmake -S . -B build-g0 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-m0p.cmake -DFE_MCU=G0B1 -DCMAKE_BUILD_TYPE=Release
cmake --build build-g0 --target bringup_kart_a bringup_kart_m
```

Çıktılar `build-*/bringup/` altına `.elf`, `.hex` ve `.bin` olarak yazılır.

- HAL / Cube kullanılmaz; sürücüler `stm32/hw.c` içinde, yazmaç düzeyinde.
- Saat HSE 8 MHz. Kristal başlamazsa HSI16 ile devam edilir ve raporda KALDI yazar.
- CAN klasik 500 kbit/s. Devreye alma kimlikleri 0x7E0–0x7E8, üretim kimlikleriyle çakışmaz.

## Yükleme

ST-LINK (V2 / V3) ile SWD üzerinden. Kart B'de J16A, Kart A'da J31, Kart M'de J3 (1,27 mm 10 pin Cortex). Örnek:

```sh
STM32_Programmer_CLI -c port=SWD -w bringup_kart_b.hex -v -rst
```

Kart M için J3 SWD **yalıtımlı tarafta**:
- yalnız modül kablosu sökülüyken ya da yalıtımlı programlayıcıyla bağlanılır;
- hasta elektrotları takılı değilken bağlanılır.

## Devreye alma sırası

Her kart için aynı sırayı izleyin. Çıktıyı terminal kaydına alın; rapor satırlarını Kabul Testi Kayıt Tablosu'na aktarın.

### 0. Enerji vermeden önce
- Gözle kontrol: köprü, ters parça, kutup (elektrolitik, TVS, diyot).
- Multimetreyle her rayın GND'ye direnci: +24V, +12V, +5V, +3V3, +3V3A, VREF. Kısa devre (< 10 Ω) varsa enerji verilmez.

### 1. İlk enerji: Kart B tek başına, kablolar takılı değil
- Laboratuvar beslemesi:
  - J1-1 +24 V, akım sınırı 0,3 A;
  - J1-2 +12 V, akım sınırı 0,5 A;
  - J1-3 GND.
- Boştaki akımı kayda yazın. Beklenen birkaç yüz mA'in altında; aşırısı enerjiyi keser.
- Test noktalarında rayları ölçün:
  - TP +5V, +3V3, +3V3A;
  - TP9 VREF 3,000 V ± 9 mV.

### 2. Firmware'i yükleyin, konsolu bağlayın
- `bringup_kart_b.hex` yüklenir.
- USB-RS232 çevirici: J20 ya da W14 kablosuyla P2 DB9.
- Açılışta `rapor` kendiliğinden çalışır.

| Kalem | Kabul |
|---|---|
| VREF+ (VREFINT ile) | 2,97–3,03 V |
| +24 V / +12 V / +5 V | 22,8–25,2 / 11,4–12,6 / 4,80–5,20 V |
| MCU sıcaklığı | 5–60 °C |
| HSE | çalışıyor |
| W5500 VERSIONR | 0x04 |
| W25Q32 JEDEC | EF4016 |
| FRAM 0x50, EEPROM 0x51 | I2C yanıt |
| MAC 0x53 | okunur, OUI ≠ 0 |
| Fan takometre | 20–400 Hz, fan açıldıktan 3 s sonra (Akasa DFS802512M ≈ 2500 d/dk ≈ 83 Hz) |
| El butonu 1. / 2. kademe | "bırakılmış" (buton takılı, basılmamış) |
| Acil stop kanal 2 | kapalı (P7 ve buton takılı, buton çekili) |

### 3. Girişler
`izle` komutu girişleri 0,5 s'de bir yazar. Her girişi tek tek değiştirin ve 0 / 1'in değiştiğini görün:
- el butonu 1. ve 2. kademe: `EL1` / `EL2` satırı bırakılmış → basılı;
- pedal A/B;
- acil stop: basınca ESTOP2 = 1;
- kapı: J21-1/2'ye 12–24 V;
- termik: J8 köprü / açık;
- senkron girişleri: J19 + / − uçlarına 12–24 V;
- fabrika butonu SW2.

El butonu kablosu kopuk ya da kısa devre ise `kopuk` / `kısa devre` yazar. Bu da denenir, çünkü takılı buton kesmesi bu ayrıma dayanır.

### 4. Genel çıkışlar
`sira` komutu her çıkışı 1 s açar: senkron (K5–K7), RL1–RL3, buzzer, RL8–RL10, kolimatör lamba ve lazer, RS-485 DE.
- Röle tıkı ve LED'leri izleyin.
- PhotoMOS çıkışlarında multimetreyi diyot / süreklilik kademesine alın: J19-7/8, 9/10, 11/12 ve J21 kontakları.
- Kolimatör: J7'ye 12 V LED lamba ya da 10 Ω / 25 W yük takın. `girisler` komutundaki `KOL_LAMBA akım` değeri lamba açıkken yükselmelidir.
- Tek çıkış: `cikis lambaisin 1`. Tüm liste: `cikis liste`.

### 5. J2 / J4 hatları ve DAC
**Önce J2 (invertör CON2) ve J4 (fokus röleleri) fişleri sökülür.** Sonra `j2onay EVET` yazılır.

- `sira` artık SOFAJ, ONISITMA, INTLK, RL5, FOKUS ve KAPSARJ çıkışlarını da 0,5 s açar.
  - J2-1, J2-2 ve J2-11'de PhotoMOS iletimi ölçülür (J2-12 GND'ye).
  - RL5'in çekmesi için acil stop kanal 1'in kapalı olması gerekir.
- `dac 300 1200` komutu DAC'a EP 0,30 V ve IP 1,20 V verir:
  - beklenen J2-5 ≈ 0,52 V, geri okuma ≈ 0,24 V;
  - beklenen J2-6 ≈ 3,94 V, geri okuma ≈ 1,16 V;
  - tolerans ±%3 + 20 mV;
  - TP4 ve TP5 multimetreyle aynı anda ölçülemez (DAC 50 ms açık). Kalıcı ölçüm için komut birkaç kez tekrarlanır.

### 6. Kart A
- Kart A'ya enerji J30 → W01 → Kart B J23 üzerinden gelir. İlk kez laboratuvar beslemesinden verilecekse J30-3 +24 V ve J30-4 GND, akım sınırı 0,4 A.
- `bringup_kart_a.hex` yüklenir. Ekranda üstte renk çubukları, altta gri basamaklar, beyaz çerçeve ve durum satırları görünür.

| Kalem | Kabul |
|---|---|
| +24V_A (MONA_24V) | 22,5–25,2 V |
| VDDA | 3,20–3,40 V |
| Panel kimliği | SATOZ 40p (R_PID2 2k2) |
| NTC kart (RT1) | 5–60 °C, oda sıcaklığına yakın; parmakla ısıtınca artar |
| NTC prob (J49) | takılıysa 5–60 °C; takılı değilse "takılı değil" (kalmaz) |
| BT817 REG_ID / yonga | 0x7C / 0x…1708; "iç osilatör" yazarsa kristali kontrol edin |
| Ekran | çubuk sırası beyaz, sarı, camgöbeği, yeşil, eflatun, kırmızı, mavi, siyah; 16 gri basamak ayrı seçiliyor; çerçeve dört kenarda tam |
| Dokunmatik | `dokunma`: ekrana basınca ham X / Y değişir, bırakınca FFFF |
| Ses | açılışta bip; `ses` ile tekrar |
| CAN1 Kart B | 0x7E2 alınıyor. Kart B konsolunda `can`: "Kart A kalp atışı" GEÇTİ, yankı hatalı = 0 |

Panel zamanlaması tipik 800 × 480 AT070 değerleridir (`eve.c`, `EVE_SATOZ_800x480`). Görüntü kayarsa ya da renkler terse dönerse SATOZ veri sayfasıyla düzeltilir:
- PCLK kutbu;
- HSYNC / VSYNC genişliği;
- SWIZZLE.

### 7. Kart M
- W16 + M01 kablosuyla P16'ya bağlanır.
- `bringup_kart_m.hex` yüklenir.
- Kart A raporunda ve ekranında şu kalemler görünür:

| Kalem | Kabul |
|---|---|
| CAN2 Kart M | 0x7E3 alınıyor |
| ADS1292R kimliği | 0x73 |
| ABP (0x28) | yanıt; hortum açıkken ≈ 0 kPa |
| SDP810 (0x25) | yanıt, CRC doğru; P2–P3 nozuluna üfleyince dp değişir |

## Komutlar

**Kart B:** `rapor`, `raylar`, `kimlik`, `can`, `fan`, `girisler`, `izle`, `cikis liste`, `cikis <ad> 1|0`, `sira`, `j2onay EVET`, `dac <mV_ep> <mV_ip>`, `sifirla`

**Kart A:** `rapor`, `ekran`, `ses`, `dokunma`, `sifirla`

## Dosyalar

| Dosya | İçerik |
|---|---|
| `common/bu_core.[ch]` | Donanımdan bağımsız parçalar, PC'de test edilir: sonuç/sınır, dönüşümler (ray, VREFINT, iç sensör, NTC), CAN kalp atışı, çıkış bekçisi, konsol |
| `common/bu_boards.[ch]` | Kart tabloları: pinler (pins_b.py rev 0.5i, karta rev 0.9a, kartm rev 0.2), sınırlar, DAC → J2 beklenen değer, panel kimliği |
| `stm32/hw.[ch]` | GPIO, UART, ADC, SPI, I2C, FDCAN, iç sensör (G474 / G0B1) |
| `stm32/startup.c`, `*.ld` | Başlangıç ve bağlayıcı betikleri |
| `board_b/main_b.c` | Kart B |
| `board_a/main_a.c`, `eve.[ch]` | Kart A, BT817 |
| `board_m/main_m.c` | Kart M |
