// encoder.c - encoder for the microphone codec (format described in CODEC.md).
//
// Block = one bit stream from bit 0; each 16-bit word is read LSB-first, words big-endian in the payload.
// pulse = [run zeros][1][a = sign][b]; b=1 -> 9-bit fine size, the pulse takes a bin (pos += 1);
// b=0 -> 3-bit coarse value v (decodes as size 32*(v+1)), the next run counts from this bin.
// size = 32*log2|C| + 177.  Every block must reach bin position >= ~52 with its trailing zeros.
#include <math.h>
#include <string.h>
#include "encoder.h"

#define OFF     177.0f
#define MAXBIN  47
#define NB1     4
#define REL1    32.0f
#define MINLEV  13.0f
#define REGION  5
#define FLOOR_U (32.0f * 60.0f / 6.02f)
#define TOP_U   (32.0f * 40.0f / 6.02f)
#define FAR     96.0f
#define TOL     4.0f
#define ENDPOS  53

static float KER[ENC_N][2 * ENC_N];
static int ker_ready;

static void ker_init(void) {
    for (int k = 0; k < ENC_N; k++)
        for (int m = 0; m < 2 * ENC_N; m++)
            KER[k][m] = (float)(cos(M_PI / ENC_N * (m + 0.5 + ENC_N / 2) * (k + 0.5)) *
                                sin(M_PI * (m + 0.5) / (2 * ENC_N)));
    ker_ready = 1;
}

void enc_init(enc_state_t *s) {
    memset(s, 0, sizeof *s);
    if (!ker_ready) ker_init();
}

void enc_mdct(const float *x, float *c) {
    for (int k = 0; k < ENC_N; k++) {
        const float *kr = KER[k];
        float acc = 0;
        for (int m = 0; m < 2 * ENC_N; m++) acc += kr[m] * x[m];
        c[k] = acc;
    }
}

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline int cval(float L) { return clampi((int)rintf(L / 32.0f) - 1, 0, 7); }   // coarse value

void enc_block(const float *c, uint8_t out[ENC_BLOCK]) {
    float L[ENC_N];
    for (int k = 0; k < ENC_N; k++) L[k] = 32.0f * log2f(fabsf(c[k]) + 1e-12f) + OFF;

    uint8_t isfine[ENC_N] = {0}, coded[ENC_N] = {0};
    int order[ENC_N], nup = 0, nfine = 0, n6 = 0, maxpos = -1;
    for (int q = 4; q < MAXBIN; q++) if (L[q] >= MINLEV) order[nup++] = q;
    if (nup) {
        for (int i = 1; i < nup; i++) {                         // stable sort, loudest first
            int q = order[i], j = i - 1;
            while (j >= 0 && L[order[j]] < L[q]) { order[j + 1] = order[j]; j--; }
            order[j + 1] = q;
        }
        float mx = L[order[0]];
        for (int i = 0; i < nup && nfine < NB1; i++)
            if (L[order[i]] >= mx - REL1) { isfine[order[i]] = coded[order[i]] = 1; nfine++;
                                            if (order[i] > maxpos) maxpos = order[i]; }
        // bit cost if one more coarse pulse at 'e' is added, and the end-position rule
        #define FITS(e) ({ int last_ = (e) > maxpos ? (e) : maxpos;                                    \
                           int fb_ = nfine - (isfine[last_] ? 1 : 0);                                  \
                           int cost_ = 48 + 12 * nfine + 6 * (n6 + 1) + (last_ - 4) - fb_;             \
                           int after_ = last_ + (isfine[last_] ? 1 : 0);                               \
                           int need_ = ENDPOS - after_; if (need_ < 6) need_ = 6;                      \
                           224 - cost_ >= need_; })
        #define ADD(e) do { coded[e] = 1; n6++; if ((e) > maxpos) maxpos = (e); } while (0)
        for (int a = 4; a < MAXBIN; a += REGION) {               // pass 1: strongest bin of each region
            int best = -1;
            for (int q = a; q < a + REGION && q < MAXBIN; q++)
                if (L[q] >= MINLEV && !isfine[q] && L[q] >= mx - FLOOR_U && (best < 0 || L[q] > L[best])) best = q;
            if (best >= 0 && FITS(best)) ADD(best);
        }
        for (int i = 0; i < nup; i++) {                           // pass 2: loudest first
            int q = order[i];
            if (coded[q] || L[q] < mx - TOP_U) continue;
            if (FITS(q)) ADD(q);
        }
        #undef FITS
        #undef ADD
    }

    // write: bins 0..3 always, then the chosen bins in order
    float mxall = L[0];
    for (int k = 1; k < MAXBIN; k++) if (L[k] > mxall) mxall = L[k];
    uint8_t bits[224 + 64];
    int nb = 0, pos = 0;
    for (int q = 0; q < ENC_N; q++) {
        int b, v;
        if (q < 4) {
            int cv = cval(L[q]);
            if (L[q] < mxall - FAR || (L[q] < 256.0f + TOL && fabsf(L[q] - 32.0f * (cv + 1)) <= TOL)) { b = 0; v = cv; }
            else { b = 1; v = clampi((int)rintf(L[q]), 0, 511); }
        } else if (coded[q]) {
            if (isfine[q]) { b = 1; v = clampi((int)rintf(L[q]), 0, 511); }
            else { b = 0; v = cval(L[q]); }
        } else continue;
        int need = (q - pos) + 3 + (b ? 9 : 3);
        if (nb + need > 224) break;                                // never happens with the rules above
        for (int z = pos; z < q; z++) bits[nb++] = 0;
        bits[nb++] = 1; bits[nb++] = c[q] < 0; bits[nb++] = (uint8_t)b;
        for (int i = 0; i < (b ? 9 : 3); i++) bits[nb++] = (v >> i) & 1;
        pos = b ? q + 1 : q;
    }
    while (nb < 224) bits[nb++] = 0;
    for (int w = 0; w < 14; w++) {
        unsigned v = 0;
        for (int i = 0; i < 16; i++) v |= (unsigned)bits[16 * w + i] << i;
        out[2 * w] = (uint8_t)(v >> 8); out[2 * w + 1] = (uint8_t)v;
    }
}

void enc_push(enc_state_t *s, const float *x48, int n, void (*emit)(const uint8_t *, void *), void *ctx) {
    for (int t = 0; t < n; t++) {
        float cur = x48[t];
        uint64_t j = s->nin++;                    // index of 'cur'
        // outputs whose position p lies in [j-1, j): need input samples j-1 and j
        while (j > 0 && s->pos125 / 125 == j - 1) {
            float fr = (float)(s->pos125 % 125) / 125.0f;
            s->x[s->nx++] = s->prev * (1.0f - fr) + cur * fr;
            s->pos125 += 288;                     // 48000 / 20833.33 = 2.304 = 288/125
            if (s->nx == 2 * ENC_N) {
                float cf[ENC_N];
                uint8_t blk[ENC_BLOCK];
                enc_mdct(s->x, cf);
                enc_block(cf, blk);
                memmove(s->x, s->x + ENC_N, ENC_N * sizeof(float));
                s->nx = ENC_N;
                if (!s->have_pend) { memcpy(s->pend, blk, ENC_BLOCK); s->have_pend = 1; }
                else {
                    uint8_t fr56[2 * ENC_BLOCK];
                    memcpy(fr56, s->pend, ENC_BLOCK); memcpy(fr56 + ENC_BLOCK, blk, ENC_BLOCK);
                    s->have_pend = 0;
                    emit(fr56, ctx);
                }
            }
        }
        s->prev = cur;
    }
}
