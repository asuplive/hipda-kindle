#include "pinyin.h"
#include "pinyin_table.h"

#include <string.h>

int pinyin_candidates(const char *prefix, int start, char out[][4], int max) {
    if (!prefix) prefix = "";
    int total = 0, filled = 0;
    for (int i = 0; i < PINYIN_N && filled < max; i++) {
        const char *py = kPinyin[i].py;
        /* prefix match: typed letters must be a prefix of the syllable */
        const char *a = prefix, *b = py;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a) continue;              /* ran out of typed letters -> no match */
        for (const char *c = kPinyin[i].chars; *c && filled < max; c += 3) {
            if (total >= start) {
                memcpy(out[filled], c, 3);
                out[filled][3] = 0;
                filled++;
            }
            total++;
        }
    }
    return total;
}
