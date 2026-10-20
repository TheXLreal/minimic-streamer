# MiniMic Streamer

**nRF5340 DK port (built-in radio):** see [the Russian setup and controls guide](NRF5340DK_RU.md).
Build with `tools/build-nrf5340.ps1`; the two-core firmware is in `dist/nrf5340dk/`.
This port compiles and passes ARM emulation checks. Flash/readback, CDC, the Windows
render endpoint, digital-silence USB playback and software ON/OFF/MUTE controls have
been verified on a DK. **Version 0.1.12 paired with the tested Nucleus 7:**
the [successful trace](diagnostics/nucleus7-0.1.12/README.md) contains eight addressed
`0x16` confirmations after four replies. It used +3 dBm and a READY/START delay sweep;
the last reply started at 6001 us after beacon END. With 0.1.14, the user **hears
computer audio after selecting Mini Microphone in the Nucleus app**. With external
antenna #1, the user reports that long dropouts stopped, but muffled and robotic/
raspy distortion remains. Captured sessions show no local queue drops or radio faults.
The current build is **0.1.15** (19 ARM emulation checks passed). It uses the requested updated audio HOP table:
`{24,76,18,68,66,36,16,44,78,20,62,22,38,48,42,28}`. The full 48-frame hopping cycle
is checked in ARM emulation. See the [build and HOP verification](diagnostics/nrf5340dk-0.1.15/README.md).
Both cores have been programmed and readback verified on the DK. USB music playback
runs without local starvation or faults in the captured session; the user tentatively
reports better sound. Residual distortion and sustained stability are not yet confirmed.
It retains +3 dBm audio, input PCM peaks and RADIO active between the two packets in a slot.
Hardware traces verify nonzero codec data and a 6144-us frame period;
[audio timing and listening diagnostics](diagnostics/nucleus7-audio-0.1.14/README.md)
record the unresolved quality/stability problem. The public logs replace the receiver serial.
The [previous unsuccessful trace](diagnostics/nucleus7-0.1.11/README.md)
remains available. RF range is unverified. `TESTS.json` records ARM emulation checks;
USB/audio checks from earlier builds do not validate RF interoperability.
The Pico instructions below describe the original implementation.

**Stream your computer's sound straight to your hearing devices.**

> **DISCLAIMER:** MiniMic Streamer is an independent project. It is not affiliated with, endorsed by or
> supported by any hearing device manufacturer or any medical company. It is not a medical device and has
> not been tested or approved for medical use. It should not harm or change your devices, but still,
> **USE IT ENTIRELY AT YOUR OWN RISK**, without any warranty (see the [licence](LICENSE)).

The MiniMic has an audio input: plug a sound source into its jack, and the sound goes wirelessly to the
hearing devices paired with it. MiniMic Streamer rebuilds that input on a Raspberry Pi Pico, with USB in
place of the jack. Plug it into a computer (Windows, macOS, Linux or Android) and it shows up as an
ordinary sound output called *MiniMic Streamer*. Everything you play there, whether a film, a video call
or music, travels over the air exactly as the MiniMic sends its jack input, and your hearing devices play
it.

> See the [compatibility list](COMPATIBILITY.md).

All the work happens on the Pico. It encodes the audio with the MiniMic's own codec and transmits on its
frequency-hopping channels, so to the hearing devices it looks just like a real MiniMic. The radio link
and the codec were worked out from scratch and are documented in [CODEC.md](CODEC.md).

The Pico sets the pace of the audio and the computer follows, so even hours of playback stay in sync,
with no drift. Pairing works the same way as with the MiniMic. Future firmware updates can be installed
without pressing any buttons.

## Getting started

1. **Build the hardware:** a Raspberry Pi Pico and an nRF24L01+ radio module, connected with seven
   wires ([Hardware](#hardware)).
2. **Flash the firmware:** download the file for your board from
   [Releases](https://github.com/peter6005/minimic-streamer/releases) ([Flashing](#flashing)).
3. **Pair your hearing devices:** switch the real MiniMic off, hold the Pico's button and restart the
   devices ([Pairing](#pairing)).
4. **Play:** choose *MiniMic Streamer* as the sound output ([Use](#use)).

> **Start with a low volume.** The sound goes straight into your ears. Turn it up slowly.

## Hardware

- A Raspberry Pi **Pico, Pico W, Pico 2 or Pico 2 W**. The **Pico 2 W (RP2350) is recommended**, and it
  is the only board tested in this beta. The RP2350 has a hardware floating-point unit, which the encoder
  uses. On the RP2040 boards (Pico / Pico W) the encoder runs in software floating-point and is much
  heavier, so they may struggle to keep up in real time; they will be tested later. Wi-Fi/Bluetooth are
  never used; on the "W" boards the wireless chip is only powered because the LED hangs off it.
- **nRF24L01+** module. Tested in this beta: the **PA+LNA version with two capacitors** across its VCC/GND
  pins (10–47 µF; without them it browns out while transmitting). The PCB-antenna version without a
  capacitor will be tested later.

Wiring (SPI0):

| nRF24L01+ | Pico |
|---|---|
| CE | GP2 |
| CSN | GP3 |
| MISO | GP4 |
| SCK | GP6 |
| MOSI | GP7 |
| VCC | 3V3 |
| GND | GND |
| IRQ | not connected |

**Switch the real MiniMic off** while the Pico transmits. In this beta both use the same address and
channels and would collide. A future version will fix this.

## Pairing

A receiver plays only the transmitter it is paired with. Pair it once with the Pico:

1. Switch the real MiniMic off.
2. **Hold the Pico's BOOTSEL button for about 1.5 seconds**, until the LED blinks fast. The pairing window
   lasts **30 seconds**. Holding the button again cancels it.
3. **Restart each receiver once** (power it off and on) within the 30 seconds. You can pair as many
   receivers as you like in one window, for example both hearing devices. On start-up each one asks to be
   paired, the Pico answers and the receiver confirms. The LED stays on for 1 second for every receiver
   that confirms.
4. After the window the LED stays on for 3 seconds if at least one receiver paired. The receivers now play
   the Pico. No audio is sent during the pairing window (the radio is busy listening).

Pairing can also be started from the PC: send `5A C3 50` (`'P'`) to the serial port.

The LED is off in normal use. It is only used for pairing (and by the `L`/`l` test commands below).

A receiver remembers one transmitter. After pairing with the Pico it no longer plays the original
MiniMic until it is paired with that again, the usual way. In this beta the Pico always pairs in slot 1;
choosing the slot is planned for a future version.

In this beta all Picos use the fixed radio address `12345678` (changing it is planned for a future
version). The receiver derives its frequency-hopping pattern from the
address, and the hop table in the firmware belongs to this address. Pairing with any other address
succeeds, but the receiver then hops on channels the Pico doesn't use. Don't run two Picos (or a Pico and
a MiniMic with this address) near each other at the same time.

## Flashing

Download the `.uf2` for your board from the repository's **Releases** page. The files are not built by
hand: GitHub Actions builds them from the tagged source ([.github/workflows/build.yml](.github/workflows/build.yml)),
and every file has a signed build-provenance attestation.

**Checking a download:** the release page shows the SHA-256 checksum next to each file. Compare it with
the checksum of the file you downloaded:

| System | Command |
|---|---|
| Windows (PowerShell) | `Get-FileHash minimic_streamer_v0.1beta_pico2_w.uf2` |
| macOS | `shasum -a 256 minimic_streamer_v0.1beta_pico2_w.uf2` |
| Linux | `sha256sum minimic_streamer_v0.1beta_pico2_w.uf2` |

To also check that a file was built by this repository's workflow (needs the [GitHub CLI](https://cli.github.com)):

```
gh attestation verify minimic_streamer_v0.1beta_pico2_w.uf2 --repo peter6005/minimic-streamer
```

| File | Board | Chip |
|---|---|---|
| `minimic_streamer_<version>_pico2_w.uf2` | Pico 2 W | RP2350 |
| `minimic_streamer_<version>_pico2.uf2` | Pico 2 | RP2350 |
| `minimic_streamer_<version>_pico_w.uf2` | Pico W | RP2040 |
| `minimic_streamer_<version>_pico.uf2` | Pico | RP2040 |

**First time:** hold BOOTSEL while plugging in the Pico, then copy the `.uf2` for your board to the drive
that appears — `RP2350` for a Pico 2 / Pico 2 W, or `RPI-RP2` for a Pico / Pico W.

**Updates, no button needed:**

```
picotool load -f -x minimic_streamer_v0.1beta_pico2_w.uf2     # the file for your board
```

`-f` asks the running firmware to reboot into BOOTSEL through the Raspberry Pi reset interface. Two
other ways also work:

- open the serial port at **1200 baud** (Arduino-style "touch");
- send the bytes `5A C3 42` (`'B'`) to the serial port.

## Use

1. Plug the Pico in. It appears as **MiniMic Streamer** (sound output) plus a serial port.
2. Switch the real MiniMic off and the receiver on (paired with the Pico, see above).
3. Pick *MiniMic Streamer* as the sound output, for the whole system or for one application.
4. Set the PC volume as usual. The card has no hardware volume control; the PC scales the audio
   before sending it.

Latency is about 50 ms on the Pico/radio side, plus the PC's and the receiver's own buffering.

## Serial port

The CDC serial port shows one status line per second:

```
[modem] mode=<idle|audio|raw> tx=<frames sent>(+<per s>) under=<silence frames>(+<per s>)
        drop=<frames dropped>(+<per s>) rst=<radio re-inits>(+<per s>) depth=<frames queued> fifo=<bytes>
        addr=<radio address> pair=<-|searching|replied|paired|timeout> beacons=<receiver beacons heard>
        paired=<receivers paired> rx=<ID of the last paired receiver>
```

(all on one line). The `(+N)` values are the change over the last second, for finding dropouts:

- `under` rising: the radio had no audio frame ready and sent silence (it also counts while nothing plays).
- `drop` rising: the frame queue overflowed.
- `rst` above 0: the radio lost its configuration and was re-initialised (power or interference).
- nothing rising but the audio drops: the Pico transmits fine and the receiving device lost the signal.

Other lines:

- `[beacon] <32 bytes in hex> t=<µs> c=<counter>`: a receiver beacon, printed during pairing and while the
  monitor (`M`) is on. `t` and `c` (time and frame counter when it was heard) are only set by the monitor.
- `[button] tap`: a short press of BOOTSEL (under 1 s), for testing the button.

Binary commands (PC → Pico):

| Bytes | Meaning |
|---|---|
| `5A A5` + 84 bytes | Raw frame: three 28-byte codec blocks (`0x18`, `0x19`, `0x1A` payload bytes 2..29). While raw frames arrive (and for 300 ms after), USB audio is ignored. This lets a PC program send its own codec data. It should send about 163 frames per second and watch `depth`. |
| `5A C3 42` (`'B'`) | Reboot into BOOTSEL (firmware update) |
| `5A C3 52` (`'R'`) | Reboot |
| `5A C3 50` (`'P'`) | Start pairing mode (same as holding BOOTSEL) |
| `5A C3 41` (`'A'`) + 4 bytes | Use another radio address until the next reboot (for experiments) |
| `5A C3 48` (`'H'`) + 16 bytes | Use another hop table until the next reboot (for experiments) |
| `5A C3 4D` (`'M'`) / `5A C3 6D` (`'m'`) | Beacon monitor on / off: listen for receiver beacons in the idle quarter of every frame and print them as `[beacon]` lines (for debugging) |
| `5A C3 4C` (`'L'`) / `5A C3 6C` (`'l'`) | LED on / off, for testing (e.g. a PC script asking the user something). Pairing still controls the LED while it runs |

## Building

Requires the Raspberry Pi Pico SDK 2.x (release builds use 2.3.1), CMake, Ninja and the ARM GCC toolchain.

**TinyUSB 0.20.0 or newer is required.** The sound card is USB Audio Class 1 (UAC1), which works without
drivers on more hosts than UAC2, and TinyUSB only supports UAC1 since 0.20.0. The Pico SDK still ships
TinyUSB 0.18.0, so update the copy inside your SDK once:

```
cd $PICO_SDK_PATH/lib/tinyusb
git fetch --depth 1 origin tag 0.20.0
git checkout 0.20.0
```

With an older TinyUSB the CMake step stops with this hint. `./build_all.sh` builds all four boards into
`dist/` as `minimic_streamer_<version>_<board>.uf2`, the same way the release workflow does. The version
is taken from `git describe` (`dev` outside a Git checkout); set `VERSION=...` to override it.

```
cd firmware
mkdir build && cd build
cmake -G Ninja -DPICO_BOARD=pico2_w ..   # or pico, pico_w, pico2; PICO_SDK_PATH must point to the SDK
ninja                                    # -> minimic_streamer.uf2
```

PC encoder tool:

```
cd codec
gcc -O2 -o pc_encode pc_encode.c encoder.c -lm
ffmpeg -i input.mp3 -ac 1 -ar 48000 -f f32le - | ./pc_encode 0 > output.frames   # 56 bytes per frame
```

## How it works (short)

The firmware is split into small modules: `nrf24.c` (radio driver), `radio.c` (the core 1 transmit loop
and pairing), `usb_audio.c` (the sound card), `cdc.c` (the serial command protocol), `board.c`
(LED, button and reset, the only board-specific file) and `main.c`. The codec lives in `codec/` and
builds on both the Pico and a PC.

- **Core 0:** TinyUSB (audio, serial, reset interface). It also runs the encoder. Audio is taken from the
  USB buffer only when the radio queue needs a frame, so the buffer level follows the radio clock. The
  feedback endpoint then makes the PC match that rate.
- **Encoder:** 48 kHz → 20833 Hz resampling, 64-bin sine-window MDCT every 3.072 ms. Each block is coded as
  a run-length list of signed pulses with logarithmic sizes, packed into 224 bits. See CODEC.md.
- **Pairing:** core 1 switches the radio to receive mode and listens for receiver beacons on the four
  pairing channels. It answers a receiver that asks to pair (see CODEC.md, section 9). Core 0 reads the
  BOOTSEL button: it briefly pauses core 1, because reading the button disconnects the flash for a moment.
  It drives the LED through the Pico W's wireless chip.
- **Core 1:** the radio loop. Every 6.144 ms it sends one frame in three slots (two audio blocks, each
  twice, plus a parity packet) on the hopping channels. With no audio it sends silence, so the
  receiver stays locked.

## Licence

MIT, see [LICENSE](LICENSE).

`firmware/usb_descriptors.h` contains a descriptor template from the TinyUSB examples (MIT licence,
© Ha Thach, tinyusb.org). The reset interface follows the Raspberry Pi Pico SDK (BSD-3-Clause).
`pico_sdk_import.cmake` comes from the Pico SDK.
