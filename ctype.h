#ifndef MY_CTYPE_H
#define MY_CTYPE_H

#define U_   0x01
#define L_   0x02
#define D_   0x04
#define S_   0x08
#define P_   0x10
#define X_   0x20
#define C_   0x40
#define B_   0x80

#define CS_  (C_|S_)
#define CSB_ (C_|S_|B_)
#define LX_  (L_|X_)
#define SB_  (S_|B_)
#define UX_  (U_|X_)

static const unsigned char __ctype_lut[256] = {
    C_,    C_,    C_,    C_,    C_,    C_,    C_,    C_,
    C_,    CSB_,  CS_,   CS_,   CS_,   CS_,   C_,    C_,
    C_,    C_,    C_,    C_,    C_,    C_,    C_,    C_,
    C_,    C_,    C_,    C_,    C_,    C_,    C_,    C_,
    SB_,   P_,    P_,    P_,    P_,    P_,    P_,    P_,
    P_,    P_,    P_,    P_,    P_,    P_,    P_,    P_,
    D_,    D_,    D_,    D_,    D_,    D_,    D_,    D_,
    D_,    D_,    P_,    P_,    P_,    P_,    P_,    P_,
    P_,    UX_,   UX_,   UX_,   UX_,   UX_,   UX_,   U_,
    U_,    U_,    U_,    U_,    U_,    U_,    U_,    U_,
    U_,    U_,    U_,    U_,    U_,    U_,    U_,    U_,
    U_,    U_,    U_,    P_,    P_,    P_,    P_,    P_,
    P_,    LX_,   LX_,   LX_,   LX_,   LX_,   LX_,   L_,
    L_,    L_,    L_,    L_,    L_,    L_,    L_,    L_,
    L_,    L_,    L_,    L_,    L_,    L_,    L_,    L_,
    L_,    L_,    L_,    P_,    P_,    P_,    P_,    C_,
};

static inline int is_alnum(int x)
{
    return (__ctype_lut[(unsigned char)x] & (U_|L_|D_));
}

static inline int is_alpha(int x)
{
    return (__ctype_lut[(unsigned char)x] & (U_|L_));
}

static inline int is_blank(int x)
{
    return (__ctype_lut[(unsigned char)x] & (B_));
}

static inline int is_cntrl(int x)
{
    return (__ctype_lut[(unsigned char)x] & (C_));
}

static inline int is_graph(int x)
{
    return (__ctype_lut[(unsigned char)x] & (D_|U_|L_|P_));
}

static inline int is_lower(int x)
{
    return (__ctype_lut[(unsigned char)x] & (L_));
}

static inline int is_punct(int x)
{
    return (__ctype_lut[(unsigned char)x] & (P_));
}

static inline int is_space(int x)
{
    return (__ctype_lut[(unsigned char)x] & (S_));
}

static inline int is_upper(int x)
{
    return (__ctype_lut[(unsigned char)x] & (U_));
}

static inline int is_print(int x)
{
    unsigned char ux = (unsigned char)x;
    return (__ctype_lut[ux] & (D_|U_|L_|P_)) || ux == ' ';
}

static inline int to_lower(int x)
{
    return is_upper(x) ? x | 0x20 : x;
}

static inline int to_upper(int x)
{
    return is_lower(x) ? x & 0x5f : x;
}

#endif /* MY_CTYPE_H */
