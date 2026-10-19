# Nucleus 7 pairing capture, firmware 0.1.11

One attempt on a PCA10095 nRF5340 DK using its native network-core radio. The board
was powered/debugged through J2 with SW9 in VDD; no USB audio was streamed. A temporary
RAM debugger override allowed radio operation without J3 and was cleared on exit.

The user switched the receiver off before the window, then on after the capture
prompt, leaving it removed from the head beside the board. Its first valid beacon
arrived 17.964354 seconds after the network pairing start, leaving **12.035646 seconds**
before the 30-second deadline. Thus the receiver was active for more than the
requested 10 seconds before the pairing window ended.

**Pairing did not succeed:** 42 CRC-valid beacons and 42 completed replies; all
received packet types were `0x1E`. No `0x16` confirmation, no trace drops, no radio
faults, and no TX timeouts. Settings were fixed throughout this attempt.

- [Readable packet/register/timing log](pairing.log)
- [Raw trace records](packets.jsonl)
- [Counts, timing metrics and image hashes](SUMMARY.json)
- [ARM emulation validation for these images](TESTS.json)

The public logs replace the receiver serial with the ten ASCII digits `0000000000`:
RX payload bytes 12..21 and TX payload bytes 6..15 (zero-based, inclusive).
All other payload bytes, addresses, registers, timestamps and counters are unchanged.
The original local capture and ZIP under `dist/` are excluded from Git.
Consequently the public payloads are explicitly redacted, not byte-for-byte originals.
Image hashes identify the original firmware; firmware images were not modified.

Every TX line contains the 32-byte payload, channel and both address byte orders.
REG TX and REG RX include all 12 requested RADIO registers, plus TXPOWER,
RXADDRESSES and SHORTS. TIME lines match each TX to its RX beacon END and provide
CPU TXEN, hardware READY/START, ADDRESS and END in network-core microseconds.
The measured beacon END -> TX READY/START was 5931 us, including 130 us TXEN ramp-up;
beacon END -> TX ADDRESS was 5950..5951 us. The configured 5800 us delay targets TXEN.

RX records are captured before payload filtering, including bad CRC and unknown
packet types. The receiver remains limited to the configured address and currently
tuned channel; this is not an all-channel RF sniffer. Emulation tests verify that
`0x16`, unknown packet types and bad-CRC packets are retained, but do not prove RF
interoperability. Previous USB/audio tests on earlier builds do not establish that
audio reaches Nucleus 7 with this firmware.

Reproduction uses `tools/capture_rf_swd.py` with the exact built network ELF and
its mailbox layout. See the root Russian guide for build and J2/J3 instructions.
