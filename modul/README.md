# Kart M – hayvan modülü firmware çekirdeği (EKG + solunum tetikleme)

Durum: çekirdek yazıldı ve PC'de yapay sinyallerle test edildi. Donanım katmanı ve önyükleyici henüz yok.
Protokolün tanımı: `FalconEye_Arayuz_Tanimi.md` §15 (rev 0.9). Tek kaynak: `include/km_proto.h`.

| Dosya | İş |
|---|---|
| `km_proto` | Kart M ↔ Kart A CAN mesajları (iki tarafta ortak), ayar CRC'si, alıcı tarafı pencere geçerliliği |
| `km_ecg` | R dalgası bulucu, 1000 örnek/s, tam sayı; tür tablosu; kalp hızı ve sinyal kalitesi |
| `km_resp` | Solunum fazı (soluk verme / alma sonu düzlüğü), nefes hızı; yastık, akış, empedans |
| `km_gate` | Tetik penceresi: ECG / RESP / BOTH, ayar denetimi |
| `km_app` | Uygulama: örnek girişi, CAN mesajı üretimi, durum, dalga akışı, komutlar |

Güvenlik: modül ışınlama başlatamaz; yalnız pencere bilgisi verir. Her hata durumu pencereyi kapalı tarafa düşürür.

Test: `ctest -R hayvan_modulu` (`tests/test_modul.c`).

Yazılacaklar:
1. Donanım katmanı (STM32G0B1CBU6): ADS1292R (SPI, 1 kSPS, elektrot kopukluk), ABP (I²C 0x28), SDP810 (I²C 0x25), FDCAN1 (PA11 / PA12, HSE 8 MHz), 1 kHz zamanlayıcı.
2. CAN önyükleyici (0x700–0x70F).
3. Kart A tarafı: 0x010 köprüsü, ayar, ekran. Kart B: pencere koşulu (`km_win_rx_usable`), E107 / E215 / E216.
4. Tür tablosu ve solunum eşiklerinin EKG simülatörü ve gerçek kayıtlarla doğrulanması.
