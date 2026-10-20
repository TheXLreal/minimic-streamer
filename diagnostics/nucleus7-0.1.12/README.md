# Nucleus 7 pairing succeeded with nRF5340 DK 0.1.12

The receiver was switched off, placed off-head directly beside the DK antenna,
then restarted after the pairing window opened. USB J2 provided power and SWD.
The requested power was +3 dBm: TXPOWER=0, VREQH=1, VREQHREADY=1 in every record.
Address mapping, 32-byte payload and legacy no-PCF packet format were unchanged.

**Result: paired=1, PAIR_DONE, eight CRC-valid 0x16 confirmations carrying our
12 34 56 78 transmitter address.** There were 31 CRC-valid RX packets (23 x 0x1E,
8 x 0x16), one bad-CRC RX packet, four TX replies, zero trace drops and zero faults.

The first three replies targeted READY/START at 5950 us after beacon END; their
measured starts were 5951 us. The fourth targeted 6000 us and started at 6001 us.
The first confirmation followed that fourth reply by 68173 us from TX END to RX END.
The remaining 6050/6100/6150/6200-us steps were not sent: the receiver had confirmed,
so firmware correctly stopped replying to that known receiver. This does not isolate
timing from increased power or changed placement. Actual RF output was not measured.

The previous 0.1.11 log's 5951 us was beacon END -> TX ADDRESS; its READY/START was
5931 us. In this successful attempt TX ADDRESS was 5971 or 6021 us. TXEN -> READY
remained 130 us; the requested targets had a measured +1-us scheduling error.

First valid beacon arrived with 7.865425 seconds left in the 30-second window.
Thus this attempt did NOT meet the suggested 10-second margin, but the addressed
confirmation was observed before window expiry. The test was not repeated after
success merely to increase the margin. RSSI ranged from -88 to -67 dBm.

After moving the cable to J3, Windows enumerated the MiniMic Audio render endpoint.
48-kHz stereo digital-silence playback passed. During user music, mode=audio,
queue and FIFO were populated, and drop/late/fault counters stayed zero. The user
heard the processor's streaming activation signal but **no computer audio**.
Subsequent normal beacons retained session 0x30 and byte29=0x4B after the DK reset.
Pairing is therefore confirmed; audible playback remains unresolved. In 0.1.12,
+3 dBm is a pairing-only diagnostic option; normal audio still transmits at 0 dBm.

Files: [pairing.log](pairing.log), [packets.jsonl](packets.jsonl),
[SUMMARY.json](SUMMARY.json), [17 ARM emulation checks](TESTS.json).
All required RADIO registers and hardware network-TIMER timestamps are logged.
The public copy replaces receiver serial bytes with ASCII 0000000000 at RX bytes
12..21 and TX bytes 6..15. All addresses, registers, timing, counters and other
payload bytes are unchanged. The unmodified original and ZIP remain under dist/.
RX captures include every completed addressed packet on the tuned channel,
including bad CRC and arbitrary byte3; this is not an all-channel sniffer.
