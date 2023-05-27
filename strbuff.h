#ifndef NKCC_STRBUFF_H
#define NKCC_STRBUFF_H

struct __nkcc_strbuff {
    char buf[8192];
    int n, max;
};

typedef struct __nkcc_strbuff strbuff_t;

void strbuff_setup(strbuff_t *sb);
void strbuff_putstr(strbuff_t *sb, const char *str, int n);
void strbuff_append_char(strbuff_t *sb, char ch);
void strbuff_append_str(strbuff_t *sb, const char *str, int n);
void strbuff_reset(strbuff_t *sb);
void strbuff_cleanup(strbuff_t *sb);

#endif
