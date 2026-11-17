/* Compact RFC 1321 MD5. Only used to derive the forum login password hash
 * (Discuz pwmd5 = md5(password) in lowercase hex). Not security-critical. */
#include "md5.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t h[4];
    uint64_t bytes;
    unsigned char buf[64];
    size_t   fill;
} Md5;

static const uint32_t K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u
};

static const unsigned char R[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static void md5_block(Md5 *m, const unsigned char *p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i * 4] | ((uint32_t)p[i * 4 + 1] << 8) |
               ((uint32_t)p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
    uint32_t a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7 * i) & 15; }
        uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rol(a + f + K[i] + w[g], R[i]);
        a = tmp;
    }
    m->h[0] += a; m->h[1] += b; m->h[2] += c; m->h[3] += d;
}

static void md5_init(Md5 *m) {
    m->h[0] = 0x67452301u; m->h[1] = 0xefcdab89u;
    m->h[2] = 0x98badcfeu; m->h[3] = 0x10325476u;
    m->bytes = 0; m->fill = 0;
}

static void md5_push(Md5 *m, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    m->bytes += len;
    while (len) {
        size_t take = 64 - m->fill;
        if (take > len) take = len;
        memcpy(m->buf + m->fill, p, take);
        m->fill += take; p += take; len -= take;
        if (m->fill == 64) { md5_block(m, m->buf); m->fill = 0; }
    }
}

static void md5_final(Md5 *m, unsigned char out[16]) {
    uint64_t bits = m->bytes * 8;
    unsigned char pad = 0x80;
    md5_push(m, &pad, 1);
    unsigned char zero = 0;
    while (m->fill != 56) md5_push(m, &zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (8 * i));
    md5_push(m, len, 8);
    for (int i = 0; i < 4; i++) {
        out[i * 4]     = (unsigned char)(m->h[i]);
        out[i * 4 + 1] = (unsigned char)(m->h[i] >> 8);
        out[i * 4 + 2] = (unsigned char)(m->h[i] >> 16);
        out[i * 4 + 3] = (unsigned char)(m->h[i] >> 24);
    }
}

void md5_hex_str(const char *s, char out[33]) {
    static const char hex[] = "0123456789abcdef";
    Md5 m;
    unsigned char d[16];
    md5_init(&m);
    if (s && *s) md5_push(&m, s, strlen(s));
    md5_final(&m, d);
    for (int i = 0; i < 16; i++) {
        out[i * 2]     = hex[d[i] >> 4];
        out[i * 2 + 1] = hex[d[i] & 15];
    }
    out[32] = 0;
}
