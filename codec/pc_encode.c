// pc_encode - run the encoder on a PC.
// stdin: float32 little-endian mono 48 kHz samples; stdout: 56-byte frames (0x18 block, then 0x19 block).
// Optional argument: gain in dB.  Build: gcc -O2 -o pc_encode pc_encode.c encoder.c -lm
// Example: ffmpeg -i song.mp3 -ac 1 -ar 48000 -f f32le - | ./pc_encode 0 > song.frames
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "encoder.h"
static void emit(const uint8_t *f, void *ctx) { (void)ctx; fwrite(f, 1, 56, stdout); }
int main(int argc, char **argv) {
    float g = argc > 1 ? powf(10.0f, (float)atof(argv[1]) / 20.0f) : 1.0f;
    enc_state_t s; enc_init(&s);
    float buf[4096]; size_t n;
    while ((n = fread(buf, 4, 4096, stdin)) > 0) {
        for (size_t i = 0; i < n; i++) buf[i] *= g;
        enc_push(&s, buf, (int)n, emit, NULL);
    }
    return 0;
}
