// ---------- 1. Every digraph as paste result and as stringize input ----------
#define paste(x, y) x %:%: y
#define str(x) %: x
#define xstr(x) str(x)

paste(:, >) :>
paste(<, :) <:
paste(<, %) <%
paste(%, >) %>
paste(%:, %:) %:%:      // ## token; must NOT be re-read as a paste operator
paste(%, :)  #

str(<:) "<:"
str(:>) ":>"
str(<%) "<%"
str(%>) "%>"
str(%:) "%:"
str(%:%:) "%:%:"
str(<: :>) "<: :>"
str(<:   :>) "<: :>"      // whitespace collapses to one space
str(  %:  %:  ) "%: %:"   // two tokens, not %:%:

// Stringizing keeps digraph spelling, even after pasting (needs indirection)
xstr(paste(<, :)) "<:"
xstr(paste(%, :)) "%:"
xstr(paste(%:, %:)) "%:%:"
str(paste(<, :)) "paste(<, :)"

// ---------- 2. Placemarkers (empty arguments) ----------
paste(%:, ) %:
paste(, %:) %:
paste(, )

// ---------- 3. Escaped newline inside digraphs (phase 2 before phase 3) ----------
#define s4(x) %\
:\
x
#define p4(x, y) x %\
:\
%\
: y
#define p5(x, y) x %:\
%: y
#define p6(x, y) x %:%\
: y

s4(12) "12"
p4(1, 2) 12
p5(1, 2) 12
p6(1, 2) 12
p4(%, :) #
str(%\
:) "%:"                    // arg spelled with splice -> still "%:"
str(<\
:) "<:"
str(%:\
%:) "%:%:"                 // splice inside, but one token
xstr(<\
:) "<:"

// Splice inside arguments being pasted
paste(%\
, :) #
paste(<, \
:) <:

// ---------- 4. Splice does NOT glue across whitespace ----------
str(% \
:) "% :"                   // '%' ' ' ':' (separate tokens)
str(%: \
%:) "%: %:"

// ---------- 5. Maximal munch / lexing ----------
str(<::) "<::"             // C: <: followed by ':'  (C++11 has a <:: special case, C has none)
str(%:%) "%:%"             // %: then %
str(%:%:%) "%:%:%"         // %:%: then %
str(%:%x) "%:%x"
str(<:<:) "<:<:"
str(<%%>) "<%%>"           // <% then %>
str(%:>) "%:>"             // %: then >

// ---------- 6. Object-like macros: # is NOT an operator, ## still is ----------
#define OBJ_HASH %: x
#define OBJ_PASTE a %:%: b
#define OBJ_PASTE2 a%\
:%\
:b
OBJ_HASH %: x               // not stringized
OBJ_PASTE ab
OBJ_PASTE2 ab

// ---------- 7. Variadic ----------
#define vs(...) %: __VA_ARGS__
#define vp(a, ...) a %:%: __VA_ARGS__
vs(<:, :>) "<:, :>"
vs() ""
vs(%:%:, %:) "%:%:, %:"
vp(x, y) xy
vp(x, ) x

// ---------- 8. Strings/chars: digraph content is not changed ----------
str("%:") "\"%:\""
str('%:') "'%:'"
str("<:\\:>") "\"<:\\\\:>\""

// ---------- 9. Digraph as directive introducer ----------
%:define Z 3
Z 3
%\
:define Z2 4
Z2 4
%:\
define Z3 5
Z3 5
%: define Z4 6
Z4 6
 %:undef Z
Z Z                       // no longer defined
%:ifdef Z2
yes_Z2 yes_Z2
%:else
no_Z2
%:endif
%:if 1 //comment
if1 if1
%:endif

// ---------- 10. NOT a directive ----------
a %:define NOT_DEF 1        // '%:' not first on line
NOT_DEF NOT_DEF
/* c */ %:define IS_DEF 1    // comment before it is fine, so this IS a directive
IS_DEF 1
%:%: foo                    // ## at line start: not a directive, just tokens (output as-is)

// ---------- 11. Digraph-delimited macro bodies ----------
#define BR(x) <:x:>
BR(1) <:1:>                 // :> is a token, not ':' '>' , so x is separated correctly
#define BRP(x) <%x%>
BRP(a) <%a%>
