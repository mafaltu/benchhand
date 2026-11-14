<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 Martin Faltus (mafaltu) -->
# For AI agents

BenchHand is a USB device that gives a program "hands" on a test bench: 5 relays (dry contacts NC/COM/NO) to press
buttons, reset or power-cycle a device under test (DUT), 4 isolated digital inputs (3-12 V = HIGH) to read DUT
signals, and a switched USB 2.0 passthrough (USB-B in, USB-A out) to unplug and replug a programmer or USB device.
You control it with plain text lines over a USB serial port (CDC ACM, no driver needed). The board itself documents
every command: send `HELP`.

## 1. Find the port

| OS | Port |
|---|---|
| Linux | `/dev/serial/by-id/usb-mafaltu_BenchHand_<serial>-if00` (stable name; `<serial>` = 24 hex characters, the MCU unique ID, same as `sn=` in `ID?`) |
| macOS | `/dev/cu.usbmodem<...>` (not tested) |
| Windows 10/11 | a `COMn` port, built-in CDC driver, USB ID `1209:0001` (not tested) |

If several boards are connected, ask the user which one (match `sn=` from `ID?`). USB ID `1209:0001` is a
pid.codes test PID and will change when a PID is assigned; match on the name `BenchHand`, not on the PID.

## 2. Open it

- Linux shell: `stty -F <port> raw -echo` first, otherwise the tty echoes the board's replies back to it.
- Python: `serial.Serial(port, timeout=1)` (pyserial). Baud rate, parity and DTR/RTS are ignored by the board.
- One program at a time. If several programs may use the port, wrap each use in `flock <port> <command>`.

## 3. Line protocol

- Send one command per line, ended by LF, CR or CRLF; max 320 characters; case-insensitive; decimal numbers;
  channel numbers only (relays 1-5, inputs 1-4).
- Exactly one reply per command, in command order. A reply is zero or more data lines starting with `# `, then one
  final line starting with `OK` or `ERR`. Read until that final line.
- `OK key=value ...` carries results. Errors are `ERR <code> <reason>: <hint>`; the hint says how to fix the call.
  `HELP ERRORS` lists the codes.
- `HELP` lists the commands, `HELP <command>` gives details and examples. In HELP pages `> ` marks what you send
  and `< ` the reply.

## 4. Example session

Recorded on a real board (`>` sent, `<` received):

```
> ID?
< OK model=BenchHand hw=r1 fw=0.1.0 proto=1 sn=005900A75943501420363441
> SET 1 ON
< OK
> PULSE 2 200
< OK
> INPUT?
< OK in=0,0,0,0 rise=0,0,0,0 fall=0,0,0,0
> SET 9 ON
< ERR 2 bad_channel: channel must be 1-5 (ALL only for SET and MAX_ON), input 1-4
```

`PULSE 2 200` closes relay 2 now and opens it 200 ms later. `STATUS?` returns the whole board state in one line
(`rly=`, `in=`, `usb=`, `fault=`, ...).

Python, minimal:

```python
import glob, serial
port = glob.glob("/dev/serial/by-id/usb-mafaltu_BenchHand_*-if00")[0]
p = serial.Serial(port, timeout=1)
def cmd(line):
    p.write((line + "\n").encode())
    reply = []
    while True:
        l = p.readline().decode().rstrip("\r\n")
        reply.append(l)
        if l.startswith(("OK", "ERR")):
            return reply
print(cmd("ID?"))
```

## 5. Rules agents must respect

- **Ask what is wired.** The board does not know what a relay or input is connected to. Ask the user for the wiring
  (e.g. "relay 1 = DUT reset to GND, input 1 = DUT status LED") and use only the channels you were given.
- **Relays switch real circuits.** Do not switch a relay you were not told about. A relay switched ON from the port
  turns OFF by itself after `MAX_ON` (default 60 s); repeat `SET <n> ON` as a keep-alive or change the limit with
  `MAX_ON` only if the user agrees.
- **After a board reset everything is off** except the USB passthrough, which comes back ON.
- **Host sleep turns everything off**, and so does Linux USB autosuspend of the board while no program has the port
  open (TLP, powertop). `suspends=` in `STATUS?` counts it. Keep the board awake with the udev rule from the firmware
  README (`power/control=on`) or keep the port open; ask the user before changing system settings.
- **Unplugging via the passthrough is not a safe eject.** `USB OFF`, `USB DISCONNECT <ms>`, `USB DATA OFF`, the USB
  button and host sleep remove the device on USB-A at once. Fine for programmers, serial adapters and HID devices; a
  mass-storage device must be unmounted first, or its filesystem can be corrupted.
- **`ext=1` in `STATUS?`** means a voltage is present on the USB-A output while the board has it off; the board then
  never switches its own power onto it. Tell the user.
- **The inputs take 3-12 V.** Do not ask the user to connect more than 12 V.
- **`REBOOT` / `BOOTLOADER`** reply `OK` and then disconnect the port. Re-open the port after about 1 s (`REBOOT`).
  `BOOTLOADER` leaves the board in USB DFU mode (`0483:df11`) for a firmware update; do it only when asked.
  pyserial: read the `OK` with `readline()`; a `read(n)` call can lose it when the port disconnects.

Full command reference: `HELP` and `HELP <command>` on the board, [PROTOCOL.md](PROTOCOL.md) (generated from
HELP), and the firmware README.

## 6. All commands (proto=1)

For protocol version proto=1 (firmware 0.1.0); HELP on the board is authoritative. Full reference: [PROTOCOL.md](PROTOCOL.md).

| Command | Arguments | What it does | Reply |
|---|---|---|---|
| `ID?` | - | Board identity | `OK model=BenchHand hw=r1 fw=<semver> proto=1 sn=<24 hex>` |
| `STATUS?` | - | Whole board state in one line, fields in fixed order (`HELP STATUS`) | `OK rly=... req=... btn=... presses=... left=... in=... usb=... usb_req=... data_req=... ext=... usb_btn=... usb_presses=... fault=... seq=... ready=... boot=... reset=... uptime=... suspends=... lp_err=... optr_err=...` |
| `SET` | `<1-5\|ALL> ON\|OFF` | Relay request; ON turns off by itself after MAX_ON ms, a repeated ON restarts that time; ON needs `ready=1` | `OK` |
| `PULSE` | `<1-5> <ms>` (1-3600000) | Relay on now, off after ms in the background; ERR 6 if ms > MAX_ON | `OK` (at once) |
| `MAX_ON` | `<1-5\|ALL> <ms>` (0-3600000, 0 = no limit) | Auto-off limit for remote ON; not stored, 60000 after every reset | `OK` |
| `MAX_ON?` | - | Read the limits | `OK max_on=<5 values in ms, channel 1 first>` |
| `INPUT?` | - | Debounced input levels and edge counters | `OK in=<4 x 0/1> rise=<4 counts> fall=<4 counts>` |
| `WAIT` | `<1-4> HIGH\|LOW\|RISE\|FALL <ms>` (1-60000) | Wait for an input condition; blocks (next command read after the reply) | `OK t=<ms>`, or `ERR 5 timeout` |
| `USB ON\|OFF` | - | USB-A output power request `usb_req` (ON after reset; ON also clears a latched fault) | `OK` (changes in the background) |
| `USB DATA ON\|OFF` | - | USB data lines request `data_req`, power unchanged | `OK` (changes in the background) |
| `USB DISCONNECT` | `<ms>` (1-3600000) | USB-A output off for ms, then back to the request | `OK` (changes in the background) |
| `SEQ` | `<step>; <step>; ...` (1-16 steps) | Timed sequence in the background; whole line checked first | `OK steps=<n> delay_ms=<sum of DELAY> async=<0/1>` |
| `SEQ ABORT` | - | Stop a running sequence | `OK`, or `OK seq=idle` if none was running |
| `REBOOT` | - | Relays and USB-A output off, OK, then the board resets and re-enumerates | `OK` |
| `BOOTLOADER` | - | Outputs off, OK, then ROM USB DFU bootloader (firmware update only) | `OK` |
| `HELP`, `?` | `[<topic>]` | Introduction and command list, or one page; `HELP ERRORS` lists the error codes | `# ` lines, then `OK` |

SEQ steps: `SET <1-5|ALL> ON|OFF`, `USB ON|OFF`, `USB DATA ON|OFF`, `DELAY <ms>` (1-3600000),
`WAIT <1-4> HIGH|LOW|RISE|FALL <ms>` (1-60000). While a SEQ runs, SET, PULSE, USB, MAX_ON, WAIT, REBOOT, BOOTLOADER
and SEQ give `ERR 7`; reading commands work. Errors are always `ERR <code> <reason>: <hint>`.
