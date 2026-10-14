# C compiler

For now, only the C preprocessor is implemented.

## Missing in the preprocessor

- Wide char and string.
- Universal Character Set.

## Non-standard features support

### Macro-generated `defined`

```c
#define A
#define D defined
#define E D(A)

#if D(A)
1
#else
2
#endif
// 1

#if E
1
#else
2
#endif
// 1
```

### Builtin macros

- `__BASE_FILE__`
- `__TIMESTAMP__`
- `__COUNTER__` (per C11 but apparently it becomes standard feature in C2y)

### GNU `, ## __VA_ARGS__`

```c
#define dbg(fmt, ...) printf(fmt, ## __VA_ARGS__)
dbg("hello\n"); // expanded to printf("hello\n");
dbg("hello %s\n", "world"); // expanded to printf("hello %s\n", world);
```
