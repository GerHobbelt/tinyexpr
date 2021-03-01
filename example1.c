#include "tinyexpr.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    const char *c = "2 * sin(3 * x) - ln(x^3 - 1) + 4";
    double x = 2; te_variable var = { "x", &x };
    te_expr* expr = te_compile(c, &var, 1, NULL);
    double r = te_eval(expr);
    printf("The expression:\n\t%s\nevaluates to:\n\t%f\n", c, r);
    return 0;
}