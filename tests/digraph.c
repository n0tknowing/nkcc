#define paste(x, y) x %:%: y
#define str(x) %: x
#define xstr(x) str(x)

paste(:, >) :>
paste(<, :) <:
paste(<, %) <%
paste(%, >) %>
paste(%:, %:) %:%:
paste(%, :)  #

str(<:) "<:"
str(:>) ":>"
str(<%) "<%"
str(%>) "%>"
str(%:) "%:"
str(%:%:) "%:%:"
str(<: :>) "<: :>"
str(<:   :>) "<: :>"
str(  %:  %:  ) "%: %:"

xstr(paste(<, :)) "<:"
xstr(paste(%, :)) "%:"
xstr(paste(%:, %:)) "%:%:"
str(paste(<, :)) "paste(<, :)"

paste(%:, ) %:
paste(, %:) %:
paste(, )

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
:) "%:"
str(<\
:) "<:"
str(%:\
%:) "%:%:"
xstr(<\
:) "<:"

paste(%\
, :) #
paste(<, \
:) <:

str(% \
:) "% :"
str(%: \
%:) "%: %:"

str(<::) "<::"
str(%:%) "%:%"
str(%:%:%) "%:%:%"
str(%:%x) "%:%x"
str(<:<:) "<:<:"
str(<%%>) "<%%>"
str(%:>) "%:>"

#define OBJ_HASH %: x
#define OBJ_PASTE a %:%: b
#define OBJ_PASTE2 a%\
:%\
:b
OBJ_HASH %: x
OBJ_PASTE ab
OBJ_PASTE2 ab

#define vs(...) %: __VA_ARGS__
#define vp(a, ...) a %:%: __VA_ARGS__
vs(<:, :>) "<:, :>"
vs() ""
vs(%:%:, %:) "%:%:, %:"
vp(x, y) xy
vp(x, ) x

str("%:") "\"%:\""
str('%:') "'%:'"
str("<:\\:>") "\"<:\\\\:>\""

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
Z Z
%:ifdef Z2
yes_Z2 yes_Z2
%:else
no_Z2
%:endif
%:if 1 //comment
if1 if1
%:endif

a %:define NOT_DEF 1
NOT_DEF NOT_DEF
/* c */ %:define IS_DEF 1
IS_DEF 1
%:%: foo

#define BR(x) <:x:>
BR(1) <:1:>
#define BRP(x) <%x%>
BRP(a) <%a%>
