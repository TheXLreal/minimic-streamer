# Nucleus 7 audio diagnostics, nRF5340 DK 0.1.14

Pairing was confirmed with firmware 0.1.12: see ../nucleus7-0.1.12/README.md.
The user subsequently reported no computer audio despite the streaming activation
signal. USB J3, 48-kHz audio, nonzero input PCM and changing codec data are verified.

Firmware 0.1.13 applied +3 dBm to normal audio as well as pairing. Its music trace
had correct packet copies/parity but 157..158-us gaps from the first packet END
to the duplicate packet START. The original Pico implementation preloads two
nRF24 TX FIFO payloads under a single CE-high interval. nRF24L01+ PS section 6.1.5
says it remains in TX mode to transmit the next queued payload.

Firmware 0.1.14 therefore keeps native RADIO in TxIdle after the first END and
uses START for the duplicate, avoiding another TXEN/ramp-up. A legacy comparison
mode and explicit gap are available through USB. Pairing replies are unchanged.

[Hardware timing](TIMING.json) verifies four complete frames / 20 packets during
music, all at configured +3 dBm. Frame period is 6144 us. Types are 18/39/19/38/1A;
copies are identical apart from type, and XOR parity is correct. PCM peaks observed
were 5942, 5361 and 2263 (16-bit input). Drop, late and fault counters stayed zero.

The traced within-slot gaps are 17..19 us, derived from the hardware ADDRESS event
minus the 20-us preamble/address duration at 2 Mbps. The second packet has no new
READY event: its logged `ready` field is explicitly a SOFTWARE TIMER capture just
before START (`kind=7`, `start_source=START_TASK`). ADDRESS/END still use hardware
DPPI captures. Tracing itself adds CPU work between packets; these samples do not
measure untraced gaps or antenna output with external RF equipment.

After selecting Mini Microphone in the Nucleus app, the user heard computer audio
with 0.1.14, but described poor quality and dropouts lasting about ten seconds.
Thus audible playback is confirmed, while usable quality/stability is unresolved.
Initial long button presses had not selected the intended source. Nucleus 7 also
cycles through telecoil and previously paired wireless accessories; the activation
signal alone does not identify which source is selected.

Runtime AUDIOGAP 1000 permits comparing the previous separate-TXEN scheduling
without reflashing. During the music portion of a subsequent 24-second telemetry
capture, PCM was nonzero, frames encoded normally and drop/late/fault counters
remained zero. Underflows increased only after USB playback became idle. Listening
results for interval comparisons must be recorded separately from these local TX
events, which cannot establish continuous reception at Nucleus 7.

Listening comparison: AUDIOGAP 1000 (separate TXEN) made audio disappear, and
restoring AUDIOGAP 0 plus reselecting Mini Microphone restored intermittent audio.
AUDIOGAP 130 also made audio disappear; AUDIOGAP 0 was restored again. These
observations favor the short within-slot gap, but do not establish a stable setting
or prove the original nRF24 FIFO inter-packet spacing. No external antenna change
has been made. See [legacy telemetry summary](LEGACY_COMPARISON.json).

[Header/register/timestamp records](TX_HEADERS.json) retain payload type/counter
and flags, but omit user music codec bytes, keeping their SHA256 instead. The full
local trace remains under dist/diagnostics/usb-0.1.14/. [TESTS.json](TESTS.json)
records 19 ARM emulation checks for the exact images; it is not a listening test.
