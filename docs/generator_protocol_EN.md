# Teknogem Generator Protocol (TGP) – Version 1

Interface between a DR console / acquisition software and the FalconEye X-ray
control panel. Matches interface definition rev. 0.5 (FalconEye_Arayuz_Tanimi.md).
Status: **draft, not released**.

## 1. Transport

| Link | Settings |
|---|---|
| Ethernet | TCP, panel is the server. Default **192.168.1.50 : 5025** (service menu). One client at a time; a second connection is accepted and closed immediately. Use a network interface separate from the detector's GigE link. |
| RS-232 | Reserved for the PZ Medical "Current"-compatible generator protocol (19200-8-N-1). TGP over RS-232 (115200-8N1) is optional, for service tools. |

## 2. Framing

```
<SEQ> <COMMAND>[ <KEY>=<VALUE> ...]*<CRC>\r\n
```

- `SEQ`: 4 decimal digits, 0000–9999, then wraps to 0000. Each side keeps its own counter. Replies (`ACK`, `NAK`, `PONG`) carry the request's SEQ as their first field.
- Fields: space separated `KEY=VALUE`, any order; omitted keys keep their current value. Decimal separator is `.`. 7-bit ASCII only.
- `CRC`: **CRC-16/CCITT-FALSE** (poly 0x1021, init 0xFFFF, no reflection, no final XOR) over all characters from the first SEQ digit up to (not including) `*`, as 4 upper-case hex digits. Check value: `"123456789"` → `29B1`.
- Maximum line length 128 bytes including `\r\n`. A bare `\n` is accepted.
- A line with a bad CRC, bad format or excessive length is discarded and counted; if its SEQ is readable the panel answers `NAK <seq> E903` (CRC / format) or `E901` (too long).
- The panel answers every command within **100 ms**. If no answer arrives within 500 ms, resend the **same SEQ** up to 3 times. A repeated SEQ is not executed again; the previous answer is repeated.
- Unsolicited panel lines (`ACTUAL`, `STATE`, `ERROR`, `INTERLOCK`, `LIMITS`) are not acknowledged. After reconnecting, refresh with `STATUS?`, `LIMITS?`, `LAST?`.

Units: `KV` 0.1 kV · `MA` 1 mA (measured `AMA` 0.1 mA) · `MS` 1 ms · `MAS` 0.01 mAs · `FO` = `L` large / `S` small · `MODE` = `RAD` / `FLUORO`.

## 3. Safety rules

These are properties of the protocol, not configuration options:

1. **No command starts preparation or exposure, and no command acknowledges a fault.** Exposure is started only by the hand switch or foot switch through the panel's hardware safety chain. `EXPOSE`, `XRAY`, `PREP`, `ACK-FAULT`, `RESET` → `NAK E905`.
2. **Control mode is chosen on the panel only.** `PC`: the console may set values and a live connection is required for preparation. `MANUAL`: values come from the panel only; `SET` / `MODE` → `NAK E907`; the console still reads status and accepts AED images.
3. In PC mode, **losing the connection during exposure cuts the exposure** (panel error E702) and cancels preparation.
4. Values cannot change during exposure (`NAK E900`). During preparation kV, mA and time may change (filament warm-up restarts); focus may not.
5. The panel checks every value against the installed tube insert, focal spot, rating chart, anode heat and generator power, regardless of what the console computed.

## 4. Commands

| Command | Reply | Notes |
|---|---|---|
| `PING` | `PONG <seq>` | Heartbeat. Send every 1 s; every 250 ms while `ST=XRAY`. |
| `HELLO` | `ACK <seq>` + `VERSION …` | |
| `VERSION?` | `VERSION FW=… HW=… TGP=1 IF=0.5 TUBE=<insert> CTRL=PC\|MANUAL LINK=TGP\|CURRENT SN=… OPT=NONE\|FLUORO\|DAP\|FLUORO+DAP` | `OPT` = options enabled in the panel's service menu. `FLUORO` only if the installed insert supports fluoroscopy. `DAP` = a DAP meter is fitted; without it the console calculates DAP itself. |
| `SET [KV=] [MA=] [MS=\|MAS=] [FO=L\|S]` | `ACK <seq>` + `ACTUAL …` or `NAK <seq> <code>` | `MS` and `MAS` are mutually exclusive. `MAS` requires `MA` (the console chooses mA); the panel computes `MS = MAS × 1000 / MA`, rounded to 1 ms. |
| `MODE M=RAD\|FLUORO` | `ACK` + `ACTUAL` / `NAK` | Same as the panel's single mode key: each mode restores its own last values (first switch to fluoroscopy: small focus, 50 kV, 2 mA, 20 s). Fluoroscopy only if `OPT` contains `FLUORO` (`E906`). Not during preparation (`E213`). |
| `STATUS?` | `STATUS ST=… CTRL=… LINK=… MODE=… ERR=<code> HEAT=<%> COOL=<s> DET=0\|1` + `ACTUAL …` | |
| `LIMITS?` | one `LIMITS …` line per focus and mode | §5.3 |
| `LAST?` | `STATE ST=DONE …` (last exposure) or `NAK E902` | |
| `DETREADY R=1\|0` | `ACK` | Detector-ready information from the console; cleared when the link is lost. |

States (`ST`): `IDLE`, `PREHEAT`, `READY`, `XRAY`, `DONE` (event only), `LOCKED` (waiting for full button release), `ERROR` (acknowledge on panel).

## 5. Panel → console lines

### 5.1 `ACTUAL KV= MA= MS= MAS= FO= MODE=`
Values now in effect; sent after every change (console, panel or correction).

### 5.2 `STATE ST=<state> ERR=<code> HEAT=<%> COOL=<s>`
On every state change. After each exposure:

`STATE ST=DONE XN=<n> KV= MA= MS= AKV= AMA= AMS= IRR= MAS= MEAS=0|1 CUT=0|1 END=<code>`

- `XN`: exposure counter, to match images to exposures.
- `AKV`, `AMA`: average measured values during exposure. `AMS`: exposure time counted by the panel. `IRR`: irradiation time from kV feedback (time above 75 % of set kV). `MAS`: integrated measured tube current.
- `MEAS=0`: no measurement available; set values reported. `CUT=1`: exposure cut (e.g. connection lost).
- `END=E000`: normal end; any other code: see the error code table in the user manual.

These values are intended for the DICOM header / dose report.

### 5.3 `LIMITS TUBE=<insert> MODE=RAD|FLUORO FO=L|S KV=<min>-<max> MA=1-<max> MS=1-<max> P=<W>`
Current units: highest kV is **100** (generator limit until verified with a kVp meter). A focus switched off in the panel's service menu (e.g. large focus on a unit that runs DF-161SBR on small focus only) has no `LIMITS` line; `SET FO=` to it → `NAK E906`.
Sent on request, on connection, and when the insert or mode changes. `P` is the maximum power for that focus and mode, limited by the generator. Single-focus inserts report `FO=L` only. The console should clamp its presets to these ranges; the panel enforces them anyway.

### 5.3a `DOSE XN=<n> DAP=<dGy·cm²> SRC=METER`
Sent after `STATE ST=DONE` (same `XN`) only when a DAP meter is fitted (`OPT` contains `DAP`) and its reading arrived (up to ~1 s after the exposure). Unit dGy·cm² (DICOM 0018,115E), 3 decimals. If no `DOSE` line arrives, the console uses its own calculated DAP (veterinary version: never sent). Meter enabled but no reading → warning `W603`. `LAST?` returns `DONE` followed by `DOSE` when available.

### 5.4 `ERROR E=<code> CLR=0|1` and `INTERLOCK DOOR=0|1 ESTOP=0|1 HS=OK|FAULT FOCUS=L|S|FAULT`

## 6. Reply codes in `NAK`

| Code | Meaning |
|---|---|
| E2xx | Value rejected by tube protection (same codes as the panel display, e.g. E201 kV below tube minimum, E203 mA too high for focus, E204 rating exceeded, E206 generator power exceeded). |
| E900 | Busy: exposure in progress. |
| E901 | Syntax / field error (unknown key, not a number, too many decimals, `MS`+`MAS`, `MAS` without `MA`, line too long). |
| E902 | No data (no exposure recorded yet). |
| E903 | CRC or framing error. |
| E904 | Unknown command. |
| E905 | Local only: must be done on the panel (exposure, fault acknowledge). |
| E906 | Feature disabled on this unit (e.g. fluoroscopy). |
| E907 | Values cannot be written: panel in MANUAL mode or console link not selected. |

## 7. Example session (CRC values are test vectors)

```
> 0001 HELLO*7242
< 0100 ACK 0001*63B5
< 0101 VERSION FW=0.9.0 HW=A TGP=1 IF=0.5 TUBE=DF-161SBR CTRL=PC LINK=TGP SN=FE0001 OPT=FLUORO+DAP*311C
> 0012 SET KV=70.0 MA=20 MS=100 FO=L*CE47
< 0102 ACK 0012*BE6D
< 0103 ACTUAL KV=70.0 MA=20 MS=100 MAS=2.00 FO=L MODE=RAD*AB5F
> 0013 SET KV=81.0 MA=200 MS=50 FO=L*664D
< 0104 NAK 0013 E203*A7BC
> 0020 PING*541D
< 0105 PONG 0020*4005
  (operator presses hand switch stage 1, then stage 2)
< 0106 STATE ST=DONE XN=152 KV=70.0 MA=20 MS=100 AKV=69.8 AMA=19.9 AMS=100 IRR=99 MAS=1.99 MEAS=1 CUT=0 END=E000*0465
< 0107 DOSE XN=152 DAP=1.234 SRC=METER*7681
```

## 8. Open points

- Behaviour of the PZ software on RS-232 in MANUAL mode (lab test).
- Numeric limits per tube insert after calibration.
