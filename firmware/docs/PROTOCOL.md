<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright (c) 2026 Martin Faltus (mafaltu) -->
# BenchHand protocol reference

Protocol version proto=1, firmware 0.1.0

Generated from firmware/src/core/help_text.c by firmware/tools/gen_protocol_md.c - do not edit by hand;
HELP on the board is authoritative. Each section is one HELP page (`HELP` alone, then `HELP <topic>`);
`> ` marks what you send and `< ` the board's reply. Regenerate: `cmake --build <host build dir> --target
protocol_md`.

## Introduction and command list

```text
BenchHand r1 - USB-controlled hardware test helper for AI agents.
It replaces a human's hands when testing a device under test (DUT):
- 5 relays (dry contacts NC/COM/NO): press buttons, reset, switch power.
- 4 isolated digital inputs (3-12 V = HIGH): read DUT signals.
- USB 2.0 passthrough (USB-B in -> USB-A out): disconnect / reconnect
  a programmer or USB device, power and data separately.
Channel numbers only; ask the user what is wired to which channel.
Safety: a relay switched ON from here turns OFF after MAX_ON (default 60 s);
nothing but the USB passthrough is switched on after a board reset;
host sleep turns everything off (suspends= in STATUS? counts it).
If the host autosuspends this USB device (e.g. TLP, powertop) while no program
has the port open, everything switches off like in PC sleep; keep it awake with
power/control=on (ask the user; the project README has a udev rule).
Watch out: ext=1 means a voltage is present on the USB-A output while it is off.
Before use: stty -F <port> raw -echo (prevents the tty echoing replies back).
Serial settings (baud, DTR) are ignored.
One command per line (CR, LF or CRLF), max 320 characters, case-insensitive;
words are separated by spaces (a tab counts as a space). Numbers are decimal.
Replies: data lines start with "# "; the last line starts with OK or ERR.
Exactly one reply per command, in command order. Errors look like
ERR <code> <reason>: <hint>; the hint says how to fix the call (HELP ERRORS).
In the HELP pages "> " marks what you send and "< " the board's reply.
Do not share this port between two programs at once; use
flock <port> <command> if several programs may use it.
Project: https://github.com/mafaltu/benchhand#for-ai-agents
Commands (send HELP <cmd> for details, e.g. HELP SEQ):
  ID?                          model, firmware version, serial number
  STATUS?                      whole board state in one line
  SET <1-5|ALL> ON|OFF         relay on / off
  PULSE <1-5> <ms>             relay on now, off after ms
  MAX_ON <1-5|ALL> <ms|0>      auto-off limit for relays (0 = none)
  MAX_ON?                      current limits
  INPUT?                       input levels and edge counters
  WAIT <1-4> HIGH|LOW|RISE|FALL <ms>   wait for an input (blocks)
  USB ON|OFF                   USB-A output on / off (power request)
  USB DATA ON|OFF              USB data lines request (kept separately)
  USB DISCONNECT <ms>          USB-A output off for ms, then back
  SEQ <step>; <step>; ...      timed sequence in the background
  SEQ ABORT                    stop a running sequence
  REBOOT                       restart the board
  BOOTLOADER                   enter USB DFU for a firmware update
  HELP [<cmd>], ?              this text; HELP ERRORS lists the error codes
```

## HELP

```text
HELP [<topic>]   (alias: ?)
HELP alone: introduction and command list. HELP <topic>: one page with
syntax, ranges, every reply field and examples.
Topics: ID STATUS SET PULSE MAX_ON INPUT WAIT USB SEQ REBOOT BOOTLOADER
        HELP ERRORS (a trailing ? is ignored: HELP STATUS? = HELP STATUS).
Reply: data lines starting with "# ", then OK.
Example:
> HELP PULSE
  (the "# " lines of the PULSE page, then)
< OK
> HELP FOO
< ERR 3 bad_argument: no such HELP topic, send HELP for the command list
```

## ID

```text
ID?   identity of the board.
Reply fields:
  model   always BenchHand
  hw      hardware revision (r1)
  fw      firmware version (semver)
  proto   protocol version of these commands (1)
  sn      serial number = MCU unique ID, 24 hex digits; the same text is the
          USB serial number (/dev/serial/by-id/...), so boards can be told apart.
Example:
> ID?
< OK model=BenchHand hw=r1 fw=0.1.0 proto=1 sn=0123456789ABCDEF01234567
> ID? 2
< ERR 3 bad_argument: unexpected extra argument, send HELP <command> for the syntax
```

## STATUS

```text
STATUS?   whole board state, one line, fields always in this order
(lists have one value per channel, channel 1 first; new fields only appended):
  rly          5 x 0/1: relay coil actually driven now
  req          5 x 0/1: remote request (SET / PULSE / SEQ)
  btn          5 x 0/1: relay button on the board held (a held button inverts req)
  presses      5 x count: relay button presses since boot
  left         5 x ms until the remote request turns OFF; 0 = request off,
               inf = on without a deadline (MAX_ON 0)
  in           4 x 0/1: input level, 1 = HIGH voltage on the isolated input
  usb          passthrough state: off, discharge (waiting for the output to drop),
               blocked (external voltage on the output), powering, no_input (no
               power on USB-B, retried every 2 s), power (power only), on, fault
  usb_req      0/1: USB ON|OFF request
  data_req     0/1: USB DATA ON|OFF request
  ext          0/1: 1 = external voltage on the USB-A output while VBUS is off
  usb_btn      0/1: USB button held (forces the passthrough off while held)
  usb_presses  count: USB button presses since boot
  fault        none, efuse (overload, input lost or overtemperature) or output
               (output dropped); latched until USB ON
  seq          idle, running:<step>/<steps>, done or aborted:<reason>
               (reason: abort, timeout, usb_<state>, bus_reset, suspend,
               unconfigured)
  ready        0/1: relays may switch (USB configured, no reset/suspend inhibit)
  boot         count: boots since power-up
  reset        cause of the last reset: por, obl, iwdg (watchdog), wwdg, lpwr,
               sw (REBOOT / BOOTLOADER), pin (reset button / NRST)
  uptime       ms awake since reset; does not count time in host sleep
               (wraps after 49.7 days)
  suspends     count: host sleeps (USB suspend) since reset
  lp_err       0/1: low-power entry failed once (board stayed awake, outputs off)
  optr_err     0/1: IWDG_STOP option byte not set, the board never enters Stop
Counters are 32-bit and wrap to 0. All fields are taken at the same moment.
Example:
> STATUS?
< OK rly=0,1,0,0,0 req=0,1,0,0,0 btn=0,0,0,0,0 presses=0,2,0,0,0 left=0,41250,0,0,0 in=1,0,0,0 usb=on usb_req=1 data_req=1 ext=0 usb_btn=0 usb_presses=0 fault=none seq=idle ready=1 boot=3 reset=pin uptime=123456 suspends=0 lp_err=0 optr_err=0
> STATUS? 1
< ERR 3 bad_argument: unexpected extra argument, send HELP <command> for the syntax
```

## SET

```text
SET <1-5|ALL> ON|OFF   remote relay request.
ON: the relay turns on now and OFF by itself after MAX_ON ms (default 60000,
  see HELP MAX_ON). A repeated SET ON restarts that time (keep-alive) and
  cancels a running PULSE timer.
OFF: request off now (always accepted, also before ready=1).
ON needs ready=1 in STATUS? (USB configured); a held relay button inverts
the request while held. Refused while a SEQ runs (ERR 7).
Relays switch ON at most one per 10 ms (lowest channel first): a later coil
waits, but its MAX_ON time still counts from the command.
Reply: OK
Example:
> SET 2 ON
< OK
> SET ALL OFF
< OK
> SET 6 ON
< ERR 2 bad_channel: channel must be 1-5 (ALL only for SET and MAX_ON), input 1-4
> SET 1 ON
< ERR 10 not_ready: relays are enabled after USB configuration
```

## PULSE

```text
PULSE <1-5> <ms>   relay on now, off after ms (1-3600000).
Rejected with ERR 6 if ms is longer than MAX_ON of that relay (MAX_ON 0 = no
limit). A later SET ON / SET OFF replaces the pulse.
Needs ready=1 (USB configured). Refused while a SEQ runs (ERR 7).
Timing has 1 ms resolution: the coil turns on in the next 1 ms tick, so it is
on for ms - 1 to ms. Relays switch ON at most one per 10 ms: a pulse started
within 10 ms of another relay's switch-on is shorter by that wait (or lost),
so keep pulses >= 20 ms.
Reply: OK (sent at once; the relay turns off in the background)
Example:
> PULSE 1 200
< OK
> PULSE 1 120000
< ERR 6 limit: PULSE longer than MAX_ON of this relay, raise MAX_ON or shorten the pulse
> PULSE 1 0
< ERR 4 out_of_range: number out of range, send HELP <command> for the limits
```

## MAX_ON

```text
MAX_ON <1-5|ALL> <ms>   auto-off limit for remote ON (0-3600000, 0 = no limit).
MAX_ON?                 read the limits.
A relay switched on by SET / PULSE / SEQ turns OFF MAX_ON ms after the ON.
Changing it while the relay is on counts from that ON (lowering below the time
already on turns it off now). Not stored: every reset returns to 60000.
Holding a relay button is not limited. Refused while a SEQ runs (ERR 7).
Reply: MAX_ON -> OK; MAX_ON? -> OK max_on=<5 values in ms, channel 1 first>
Example:
> MAX_ON 2 5000
< OK
> MAX_ON?
< OK max_on=60000,5000,60000,60000,60000
> MAX_ON 1 4000000
< ERR 4 out_of_range: number out of range, send HELP <command> for the limits
```

## INPUT

```text
INPUT?   input levels and edge counters.
Reply fields (4 values each, input 1 first):
  in     0/1: 1 = HIGH voltage (3-12 V) on the isolated input
  rise   count of LOW->HIGH changes since boot (32-bit, wraps)
  fall   count of HIGH->LOW changes since boot (32-bit, wraps)
Levels are debounced (3 equal samples, 1 ms apart); shorter pulses are missed.
Example:
> INPUT?
< OK in=1,0,0,0 rise=3,0,0,0 fall=2,0,0,0
> INPUT? 1
< ERR 3 bad_argument: unexpected extra argument, send HELP <command> for the syntax
```

## WAIT

```text
WAIT <1-4> HIGH|LOW|RISE|FALL <ms>   wait for an input condition (ms 1-60000).
HIGH / LOW: return at once if already true, else when the level is reached.
RISE / FALL: a new edge after the command was received.
The board blocks: the next command is read only after this reply. Relays,
timers and the passthrough keep running. Refused while a SEQ runs (ERR 7;
use a WAIT step inside the SEQ instead). Ended early with ERR 11 by a USB
bus reset, host sleep or deconfiguration (relays were switched off).
Reply: OK t=<ms from the command to the condition>, or ERR 5 on timeout.
Example:
> WAIT 1 HIGH 2000
< OK t=302
> WAIT 2 FALL 500
< ERR 5 timeout: input condition not met within the WAIT time
> WAIT 1 UP 100
< ERR 3 bad_argument: condition must be HIGH, LOW, RISE or FALL
< ERR 11 aborted: bus_reset, state was reset
```

## USB

```text
USB ON|OFF            USB-A output on / off: the power request usb_req; default
                      ON after reset. USB ON also clears a latched fault
                      (fault=efuse|output).
USB DATA ON|OFF       data lines request data_req; power unchanged (re-enumerate
                      a device). The data lines connect only while the output
                      is powered (usb=on needs usb_req=1 and data_req=1).
usb_req and data_req are independent and both kept: USB OFF / USB ON does not
change data_req (default 1), so after USB DATA OFF the output comes back as
usb=power until USB DATA ON.
USB DISCONNECT <ms>   output off for ms (1-3600000), then back to the request.
Data connects only after power is good; off always means data first, then power.
A held USB button also forces the output off. Host sleep switches it off;
it comes back after wake if requested. ext=1 means another source drives the
USB-A output: the board waits (usb=discharge, then usb=blocked) and never
switches power onto it. Check usb= and fault= in STATUS? after a change.
Removing a storage device without unmounting it first can corrupt its filesystem.
Refused while a SEQ runs (ERR 7).
Reply: OK (the state changes in the background; usb=on takes about 20 ms)
Example:
> USB DISCONNECT 2000
< OK
> USB DATA OFF
< OK
> USB OFF
< OK
> USB ON
< OK
  (now STATUS? shows usb=power data_req=0: data stays off until USB DATA ON)
> USB PAUSE
< ERR 3 bad_argument: expected USB ON|OFF, USB DATA ON|OFF or USB DISCONNECT <ms>
```

## SEQ

```text
SEQ <step>; <step>; ...   timed sequence (1-16 steps), runs in the background.
SEQ ABORT                 stop it.
Steps: SET <1-5|ALL> ON|OFF, USB ON|OFF, USB DATA ON|OFF, DELAY <ms>
       (1-3600000), WAIT <1-4> HIGH|LOW|RISE|FALL <ms> (1-60000).
The whole line is checked first; nothing starts on an error.
A step ends: SET when applied; USB OFF / DATA OFF when the output is off;
USB ON when power is good (usb=power|on), USB DATA ON at usb=on (fails on
no_input, blocked, fault or after 1 s); DELAY after ms; WAIT when the
condition holds (fails with reason timeout).
On any end except success (SEQ ABORT, a failed step, USB reset/sleep): every
relay the SEQ switched goes OFF and the USB requests return to their values
from before the SEQ. A relay left on at the end keeps its MAX_ON time.
Rejected with ERR 6 if DELAY/WAIT time while a relay is on exceeds its MAX_ON.
Times are MCU-exact to 1 ms, but relays switch ON at most one per 10 ms: a
coil switched ON within 10 ms of another starts late by that wait while its
DELAY already counts, so keep relay ON times >= 20 ms.
SET ... ON needs ready=1. While it runs, SET, PULSE, USB, MAX_ON, WAIT, REBOOT,
BOOTLOADER and SEQ give ERR 7; reading commands work. Progress: seq= in STATUS?.
Reply fields:
  steps      number of steps
  delay_ms   sum of the DELAY steps
  async      1 if a USB or WAIT step can make it longer than delay_ms
SEQ ABORT reply: OK, or OK seq=idle if no SEQ was running.
Example (hold relay 1, pulse relay 2 for 100 ms, release relay 1):
> SEQ SET 1 ON; DELAY 50; SET 2 ON; DELAY 100; SET 2 OFF; DELAY 50; SET 1 OFF
< OK steps=7 delay_ms=200 async=0
> SEQ USB OFF; DELAY 1000; USB ON
< OK steps=3 delay_ms=1000 async=1
> SEQ ABORT
< OK seq=idle
> SET 1 ON
< ERR 7 busy: SEQ running, send SEQ ABORT or wait
> SEQ SET 1 ON; DELAY 70000
< ERR 6 limit: SEQ keeps a relay ON longer than its MAX_ON, raise MAX_ON or shorten DELAY/WAIT
```

## REBOOT

```text
REBOOT   restart the board.
Relays go off and the USB-A output is switched off first; OK comes after
that, then the board resets and enumerates again (the port may get a new
name). After the reset relays are off, MAX_ON is 60000 and the USB output
turns on again. reset=sw in STATUS? afterwards. Refused while a SEQ runs.
Reply: OK
Example:
> REBOOT
< OK
< ERR 7 busy: SEQ running, send SEQ ABORT or wait
```

## BOOTLOADER

```text
BOOTLOADER   enter the ROM USB DFU bootloader for a firmware update.
Relays and the USB-A output are switched off, OK is sent, then the board
leaves this serial port and appears as an STM32 DFU device
(dfu-util -l). It returns to normal after a reset or power cycle.
Use only when the user asked for a firmware update. Refused while a SEQ runs.
Reply: OK
Example:
> BOOTLOADER
< OK
> BOOTLOADER NOW
< ERR 3 bad_argument: unexpected extra argument, send HELP <command> for the syntax
```

## ERRORS

```text
Error replies: ERR <code> <reason>: <hint>. Code and reason are fixed; the
hint is free text for you to read. Codes:
   1 unknown_command  command word not known (send HELP)
   2 bad_channel      relay not 1-5 (or ALL where allowed), input not 1-4
   3 bad_argument     wrong word, missing / extra argument, number not decimal
   4 out_of_range     number outside the allowed range
   5 timeout          WAIT condition not met in time
   6 limit            PULSE or SEQ would keep a relay on longer than MAX_ON
   7 busy             a SEQ is running: send SEQ ABORT or wait for seq=done
   8 fault            reserved
   9 line_too_long    line longer than 320 characters (whole line ignored)
  10 not_ready        relays not allowed yet (USB not configured / just reset)
  11 aborted          WAIT ended by a USB bus reset, host sleep or
                      deconfiguration; relays were switched off
Example:
> PULSE 7 100
< ERR 2 bad_channel: channel must be 1-5 (ALL only for SET and MAX_ON), input 1-4
> PULSE 1 -5
< ERR 3 bad_argument: number must be decimal digits only
```
