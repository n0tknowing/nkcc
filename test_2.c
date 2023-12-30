#define A A B
#define B C A
#define C(x) 5 x

C(A)

#define PLUS +
#define EMPTY
#define f(x) =x=
+PLUS -EMPTY- PLUS+ f(=)

#define foo bar
#define bar EMPTY baz
[foo] EMPTY;

#define add(x, y, z) x + y +z;
sum = add (1,2, 3);
