#ifndef SUBSTRINGS_H_
#define SUBSTRINGS_H_ 1

// TODO(mortehu): Move all these into a struct

extern const char *input0, *input1;
extern size_t input0_size, input1_size;

extern int skip_samecount_prefixes;
extern int do_probability;
extern int do_unique;
extern int do_document;
extern int do_color;
extern int do_cover;
extern int do_words;
extern double prior_bias;
extern double threshold;
extern int threshold_count;
extern int cover_threshold;

extern int stdout_is_tty;

void
find_substring_frequencies(size_t input0_threshold, size_t input1_threshold);

#endif  // !SUBSTRINGS_H_
