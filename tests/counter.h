#define CONCAT_IMPL(A, B) A ## B
#define CONCAT(A, B) CONCAT_IMPL(A, B)
#define UNIQUE_ID(NAME) CONCAT(NAME, __COUNTER__)

#define SWAP_IMPL(A, B, PTRA, PTRB, TMP) \
   do {                      \
        auto PTRA = &(A);    \
        auto PTRB = &(B);    \
        auto TMP  = *PTRA;   \
        *PTRA     = *PTRB;   \
        *PTRB     = TMP;     \
   } while (false)

/* A and B are modifiable and addressable lvalues. */
#define SWAP(A, B)           \
  SWAP_IMPL(A, B,            \
      UNIQUE_ID(ptr),        \
      UNIQUE_ID(ptr),        \
      UNIQUE_ID(val))

SWAP(X[35], y);

#undef __COUNTER__
#define __COUNTER__ 2147483647

__COUNTER__
