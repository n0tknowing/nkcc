/* ---- locale-free ctype.h ------------------------------------------------ */

#ifndef NKCC_CTYPE_H
#define NKCC_CTYPE_H

#ifdef __GNUC__
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic push
#endif
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static inline int isodigit(int ch)
{
    return (unsigned int)ch - '0' < 8;
}

static inline int isdigit(int ch)
{
    return (unsigned int)ch - '0' < 10;
}

static inline int isxdigit(int ch)
{
    return isdigit(ch) || ((unsigned int)ch | 32) - 'a' < 6;
}

static inline int isupper(int ch)
{
    return (unsigned int)ch - 'A' < 26;
}

static inline int islower(int ch)
{
    return (unsigned int)ch - 'a' < 26;
}

static inline int isalpha(int ch)
{
    return islower(ch) || isupper(ch);
}

static inline int isalnum(int ch)
{
    return isalpha(ch) || isdigit(ch);
}

static inline int isspace(int ch)
{
    return ch == ' ' || (unsigned int)ch - '\t' < 5;
}

static inline int ispunct(int ch)
{
    return ((unsigned int)ch - 33 < 94) && !isalnum(ch);
}

static inline int tolower(int ch)
{
    return isupper(ch) ? ch | 32 : ch;
}

#ifdef __GNUC__
#if (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic pop
#endif
#endif

#endif
