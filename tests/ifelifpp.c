#define A
#define D defined
#define E D(A)

#if D(A)
1
#else
2
#endif

#if E
1
#else
2
#endif
