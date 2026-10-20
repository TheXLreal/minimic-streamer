# Compatibility list

What has been tested with MiniMic Streamer so far. Tried it with something not listed here? Please open
an [issue](https://github.com/peter6005/minimic-streamer/issues) with the device, the firmware version and what
happened, and it will be added.

**Status:** ✅ works · ⚠️ works with problems (see notes) · ❌ does not work · 🟡 should work in theory, not
tested yet · ❔ not tested yet

## Hearing devices

| Device | Status | Notes |
|---|---|---|
| Cochlear Nucleus 8 | ✅ | Pairing and streaming tested |
| Cochlear Nucleus 8 Nexa | 🟡 | |
| Cochlear Nucleus 7 | 🟡 | |
| Cochlear Nucleus 6 | 🟡 | |
| Cochlear Kanso 3 Nexa | 🟡 | |
| Cochlear Kanso 3 | 🟡 | |
| Cochlear Kanso 2 | 🟡 | |
| Cochlear Kanso | 🟡 | |
| Cochlear Baha 6 Max | 🟡 | |
| Cochlear Baha 5 | 🟡 | |
| Cochlear Baha 5 Power | 🟡 | |
| Cochlear Baha 5 SuperPower | 🟡 | |
| Cochlear Osia 2 | 🟡 | |
| ReSound LiNX Quattro | 🟡 | |
| ReSound LiNX 3D | 🟡 | |
| ReSound ENZO 3D | 🟡 | |
| ReSound LiNX2 | 🟡 | |
| ReSound ENZO2 | 🟡 | |

Nucleus, Nexa, Kanso, Baha and Osia are trademarks of Cochlear Limited. ReSound, LiNX Quattro, LiNX 3D,
ENZO 3D, LiNX2 and ENZO2 are trademarks of GN Hearing. They are named here only to describe compatibility;
this project is not affiliated with either company.

## Pico boards

The nRF5340 DK port uses its built-in radio and has been compiled and checked in ARM emulation.
Flash/readback, CDC and a Windows render endpoint have been verified on a DK.
USB digital-silence playback and software ON/OFF/MUTE controls also pass on hardware.
**nRF5340 DK + Nucleus 7: pairing and audible computer audio confirmed; quality/stability unresolved.**
The +3-dBm timing-sweep attempt received eight addressed `0x16` confirmations;
see the [capture](diagnostics/nucleus7-0.1.12/README.md). With 0.1.14, selecting Mini
Microphone in the Nucleus app enables audible audio. External antenna #1 stopped
the long dropouts in the user's listening test, but muffled and robotic/raspy
distortion remains. See [audio diagnostics](diagnostics/nucleus7-audio-0.1.14/README.md).
Version 0.1.15 uses the requested updated audio HOP table (indices 3, 7 and 8
changed). Both cores are programmed and readback verified, and USB audio is active.
The user tentatively reports better sound, with slight robotic quality and muffled
sound remaining; sustained stability is unconfirmed.
See [0.1.15 results](diagnostics/nrf5340dk-0.1.15/README.md).
Hardware buttons, long-term stability
and RF range remain unverified.
The hearing-device results above belong to the original Pico 2 W + nRF24L01+ firmware.
See [NRF5340DK_RU.md](NRF5340DK_RU.md).

| Board | Status | Notes |
|---|---|---|
| Pico 2 W | ✅ | |
| Pico 2 | ❔ | |
| Pico W | ❔ | No hardware floating-point unit, may not keep up |
| Pico | ❔ | No hardware floating-point unit, may not keep up |

## nRF24L01+ modules

| Module | Status | Range | Notes |
|---|---|---|---|
| nRF24L01+ PA+LNA | ✅ | | With two capacitors across VCC/GND |
| nRF24L01+ with PCB antenna | ❔ | | Without a capacitor |
