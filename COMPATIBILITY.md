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
