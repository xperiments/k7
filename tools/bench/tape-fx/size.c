#include <stdio.h>
#include "tape_fx.h"
int main(void){ printf("sizeof tape_fx_t = %zu B (dly %zu B)\n", sizeof(tape_fx_t), sizeof(((tape_fx_t*)0)->dly)); return 0; }
