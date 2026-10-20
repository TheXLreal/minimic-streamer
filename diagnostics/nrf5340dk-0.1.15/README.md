# nRF5340 DK 0.1.15: updated audio HOP table

Version 0.1.14 produced audible but distorted audio with Nucleus 7. Version
0.1.15 uses the supplied replacement channel order to test whether the old HOP
table contributed to the problem:

```
{24,76,18,68,66,36,16,44,78,20,62,22,38,48,42,28}
```

| Zero-based index | Previous channel | Updated channel |
|---|---:|---:|
| 3 | 44 | 68 |
| 7 | 78 | 44 |
| 8 | 68 | 78 |

The transmitter address remains `12345678`. The schedule, codec, pairing, +3 dBm
power setting and AUDIOGAP 0 are unchanged. The application reports version
0.1.15 and USB bcdDevice 0x0115. The original Pico driver remains a reference
snapshot with its previous table.

Both cores compiled successfully. [TESTS.json](TESTS.json) records 19 passing
ARM emulation checks for the exact images, including all channels, copies and
packet counters over 49 frames (a complete 48-frame cycle and its wrap).
[HOP_CHANGE.json](HOP_CHANGE.json) verifies the table in the built network image:
only the three bytes above differ from 0.1.14; every other network flash byte is
identical. These checks do not establish reception or audio quality at Nucleus 7.

The images are packaged locally in `dist/nrf5340dk/` and
`dist/minimic_nrf5340dk_bundle.zip`. The complete 0.1.14 package was saved in
`dist/firmware-0.1.14/` before rebuilding. Previous hardware results remain under
their original versions and image hashes; none are attributed to 0.1.15.

Hardware flashing and listening are pending. Connect the DK through J2 / DEBUG
USB with SW9 in VDD to program both new images. Then return the cable to J3 /
nRF USB, select MiniMic Audio in Windows and Mini Microphone in the Nucleus app,
and compare the same audio at the same volume and placement. Re-pairing is not
expected to be necessary because the transmitter address has not changed.
