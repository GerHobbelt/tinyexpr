#include "tinyexpr.c"
#include <stdio.h>

#include "monolithic_examples.h"


#if defined(BUILD_MONOLITHIC)
#define main      tiny_expr_example1_main
#endif

int main(void)
{
    const char *c = "sqrt(5**2 * 2 + 7**2 + 11**2 + (8 - 2)**2)";
    double r = te_interp(c, 0);
    printf("The expression:\n\t%s\nevaluates to:\n\t%f\n", c, r);
    return 0;
}
