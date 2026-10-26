# Teknogem Generator Protocol (TGP) – Version 1

Interface between a DR console / acquisition software and the FalconEye X-ray
control panel. Draft for integration partners. Status: **draft, not released**.

## 1. Transport

| Link | Settings |
|---|---|
| Ethernet | TCP, panel is the server. Default port **5025**. One client at a time. Fixed IP, set in the service menu (default 192.168.10.20/24). Use a dedicated network interface, not the detector's GigE link. |
| RS-232 | DB9 on the panel, 115200 baud, 8N1, no flow control. |

Both links use the same line-based ASCII command set.

## 2. Framing

- One command per line, terminated by `\n` (a preceding `\r` is ignored). Maximum line length 127 characters.
- Every command gets exactly one reply line: `OK ...` or `ERR <code> <text>`.
- Optional checksum (recommended on RS-232): append `*HH`, where `HH` is the XOR of all characters before `*`, as two hex digits (NMEA style). Example: `SET KV=60*44`. A line with a wrong checksum is rejected with `ERR 903` and not executed.
- Numbers use `.` as decimal separator. kV and mA are reported with one decimal, mAs with two.

## 3. Safety rules

These are properties of the protocol, not configuration options:

1. **There is no command that starts preparation or exposure.** Exposure is only started by the hand switch or foot switch, through the panel's hardware safety chain. `EXPOSE`, `XRAY` and `PREP` are answered with `ERR 905`.
2. **Faults can only be acknowledged on the panel.** `ACK` and `RESET` are answered with `ERR 905`.
3. Settings cannot change during preparation or exposure (`ERR 900`).
4. If no valid line is received for **3 s**, the link is considered lost and any detector-ready status sent by the console is cleared. Send `HB` at least once per second.

## 4. Commands

| Command | Reply | Notes |
|---|---|---|
| `PING` / `HB` | `OK` | Heartbeat. |
| `VER` | `OK VER FW=<fw> PROTO=1` | |
| `SET [KV=<kV>] [MA=<mA>] [MS=<ms>] [FOCUS=L\|S] [MODE=RAD\|FLU]` | `OK SET KV=.. MA=.. MS=.. FOCUS=.. MODE=..` | Omitted keys keep their current value. The reply echoes the values now in effect. Rejected values are not applied. |
| `GET SET` | `OK SET ...` | Current settings. |
| `GET STATUS` | `OK STATUS <state> ERR=<code> HEAT=<%> COOL=<s> LINK=<0\|1>` | `HEAT` = anode heat as % of capacity; `COOL` = remaining mandatory cooling wait. |
| `GET LAST` | `OK EXPOSURE ...` (see 5.2) | Last exposure record. |
| `DETREADY 1\|0` | `OK DETREADY <0\|1>` | Detector ready status from the console (AED detectors). When the panel is configured to require it, the ready lamp does not light until `DETREADY 1` is received. |

States: `IDLE`, `PREP`, `READY`, `EXPOSING`, `LOCKED` (waiting for full button release), `FAULT` (acknowledge on panel).

## 5. Events (panel → console)

### 5.1 State change

`EVT STATE <state> ERR=<code> HEAT=<%> COOL=<s>`

### 5.2 Exposure record (after every exposure)

`EVT EXPOSURE KV=<set> MA=<set> MS=<set> FOCUS=<L|S> MODE=<RAD|FLU> AKV=<avg kV> AMA=<avg mA> AMS=<timer ms> IRR=<measured ms> MAS=<measured mAs> END=<code>`

- `AKV`, `AMA`: average measured values during exposure.
- `AMS`: exposure time counted by the panel. `IRR`: irradiation time measured from kV feedback (time above 75 % of set kV).
- `MAS`: integrated measured tube current.
- `END=E000`: normal end. Any other code: see the error code table in the user manual (E1xx–E5xx, W6xx).

These values are intended for the DICOM header / dose report.

## 6. Error codes in replies

| Code | Meaning |
|---|---|
| 2xx | Setting rejected by tube protection (same codes as the panel display, e.g. 201 = kV below tube minimum, 203 = mA too high for focus, 204 = tube rating exceeded, 206 = generator power exceeded). |
| 900 | Busy: preparation or exposure in progress. |
| 901 | Syntax error (unknown key, not a number, line too long). |
| 902 | Missing data (e.g. no exposure recorded yet). |
| 903 | Checksum error. |
| 904 | Unknown command. |
| 905 | Local only: action must be done on the panel (exposure, fault acknowledge). |
| 906 | Feature disabled on this unit (e.g. fluoroscopy). |

## 7. Example session

```
> VER
< OK VER FW=0.7 PROTO=1
> SET KV=70 MA=20 MS=100 FOCUS=L MODE=RAD
< OK SET KV=70.0 MA=20.0 MS=100 FOCUS=L MODE=RAD
> DETREADY 1
< OK DETREADY 1
  (operator presses hand switch stage 1, then stage 2)
< EVT STATE PREP ERR=E000 HEAT=0 COOL=0
< EVT STATE READY ERR=E000 HEAT=0 COOL=0
< EVT STATE EXPOSING ERR=E000 HEAT=0 COOL=0
< EVT EXPOSURE KV=70.0 MA=20.0 MS=100 FOCUS=L MODE=RAD AKV=69.9 AMA=19.9 AMS=100 IRR=99 MAS=1.99 END=E000
< EVT STATE LOCKED ERR=E000 HEAT=1 COOL=4
> SET KV=100
< OK SET KV=100.0 MA=20.0 MS=100 FOCUS=L MODE=RAD
```

## 8. Open points

- TCP port and default IP to be confirmed with the DR software vendor.
- Whether PZDR supports this protocol or requires its own generator protocol (requested from PZ Medical).
