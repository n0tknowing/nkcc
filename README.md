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
