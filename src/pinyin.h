#ifndef HIPDA_PINYIN_H
#define HIPDA_PINYIN_H

/* Collect tone-less pinyin candidates. `prefix` is the user's typed letters
 * (may be an incomplete syllable: "n" matches na/nai/nan/...). Candidates
 * from all matching syllables, in table order (GB2312 level-1). Fills up to
 * `max` UTF-8 characters (3 bytes each) starting at `start`; returns the
 * total number of matching candidates. */
int pinyin_candidates(const char *prefix, int start, char out[][4], int max);

#endif /* HIPDA_PINYIN_H */
