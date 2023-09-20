#include <stdio.h>

int main(void)
{
    float x = .12f;
    printf("%f\n", x);
    x = 12.90;
    x = 12.;
    x = 12e1f;
    x = 12e-1;
    x = .12.;
    return 0;
}
