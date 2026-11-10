/* STM32G0B1: Kart M G0B1CBU6 128 KB flash (Kart A G0B1RET6 512 KB – küçük olan kullanılır), SRAM 144 KB → 128 KB */
MEMORY { FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 128K
         RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K }
INCLUDE common.ld
