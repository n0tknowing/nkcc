#define MAP_OUT
#define A(x) x B MAP_OUT (x)
#define B(x) x A MAP_OUT (x)

#define EVAL0(...) __VA_ARGS__
#define EVAL1(...) EVAL0 (EVAL0 (EVAL0 (__VA_ARGS__)))
#define EVAL2(...) EVAL1 (EVAL1 (EVAL1 (__VA_ARGS__)))
#define EVAL3(...) EVAL2 (EVAL2 (EVAL2 (__VA_ARGS__)))
#define EVAL4(...) EVAL3 (EVAL3 (EVAL3 (__VA_ARGS__)))
#define EVAL(...)  EVAL4 (EVAL4 (EVAL4 (__VA_ARGS__)))

EVAL (A (blah))
