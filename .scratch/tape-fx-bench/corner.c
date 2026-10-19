/* Does the LFO table read past its end? */
#include <stdio.h>
#include <math.h>
#include "tape_fx.c"

int main(void)
{
    static tape_fx_t t;
    tape_fx_init(&t, 44100.f);
    /* the largest float below 2*pi, and the exact value */
    float hi = 6.28318548f, ph = hi;
    printf("2*pi as float = %.9g\n", (double)hi);
    for (int k = 0; k < 4; k++) {
        float f = ph * ((float)TAPE_SIN_SIZE / (2.f * (float)M_PI));
        printf("  ph=%.9g  f=%.9g  (int)f=%d\n", (double)ph, (double)f, (int)f);
        ph = nextafterf(ph, 0.0f);
    }
    /* Hammer the table with phases in the last few ulps. */
    float p = nextafterf(2.f * (float)M_PI, 0.0f);
    for (int k = 0; k < 64; k++) {
        volatile float y = tape_sin(p);
        (void)y;
        p = nextafterf(p, 0.0f);
    }
    volatile float z = tape_sin(2.f * (float)M_PI);
    printf("sin(2pi) = %.9g\n", (double)z);
    return 0;
}
