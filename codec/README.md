# codec

The MiniMic audio codec **encoder**, as portable C. It has no Pico or SDK dependencies (only
`<math.h>`, `<string.h>`, `<stdint.h>`), so it builds unchanged on the Pico firmware and on a PC or any
other platform. The bit-stream format it produces is documented in [../CODEC.md](../CODEC.md).

| File | What |
|---|---|
| `encoder.h` / `encoder.c` | the encoder: 48 kHz mono float in, 56-byte codec frames out |
| `pc_encode.c` | a tiny PC front end (reads float32 samples on stdin, writes codec frames on stdout) |

The Pico firmware (`../firmware`) compiles `encoder.c` directly from here.

## Build the PC tool

```
gcc -O2 -o pc_encode pc_encode.c encoder.c -lm
# example: encode a song to codec frames
ffmpeg -i song.mp3 -ac 1 -ar 48000 -f f32le - | ./pc_encode 0 > song.frames
```
