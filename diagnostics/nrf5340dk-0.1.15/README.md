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

Both cores have now been programmed on the DK through J2 / DEBUG USB using
Nordic nRF Util, with VERIFY_READ for both images and a final system reset.
[HARDWARE.json](HARDWARE.json) records this partial validation for the exact image
hashes. J3 now exposes a working Windows MiniMic Audio render endpoint and COM8;
the running firmware reports 0.1.15 and the network core is ready.

[USB_STATUS.json](USB_STATUS.json) records an eight-second capture during audio:
1338 frames were encoded and transmitted, the queue stayed populated, and
underflow, queue-drop, late-frame and radio-fault counters did not increase. Input
PCM peaks ranged from 249 to 10201. The configuration remained +3 dBm / AUDIOGAP 0.
These are local transmitter observations, not receiver audio acknowledgements.
The status field `paired=0` counts confirmations since the latest board boot;
it does not query the receiver's saved pairing.

The user tentatively reports that audio seems better with 0.1.15. Follow-up
listening describes slight robotic quality and muffled sound remaining. Dropout
continuity was not explicitly clarified. This is partial improvement, not a
confirmed solution to the quality problem.

Both the Pico and nRF5340 builds compile the shared `codec/encoder.c`. A Pico +
nRF24 reference comparison is deferred until the user's hardware is available
(expected in about five days). It must use the same corrected HOP table, address
and input audio; the original Pico snapshot's old HOP order would confound that
comparison. The present evidence does not identify the remaining distortion as
either a codec problem or an nRF5340 radio problem.

Return the cable to J3 / nRF USB, select MiniMic Audio in Windows and Mini
Microphone in the Nucleus app, and compare the same audio at the same volume and
placement. Re-pairing is not expected to be necessary because the transmitter
address has not changed.
