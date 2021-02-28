#include "tinyexpr.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
    const char *expression_string = "(sin(pi*x))^2";
    double x = 0.5; te_variable var = { "x", &x }; int error;
    
    te_expr* expr = te_compile(expression_string, &var, 1, &error);
    printf("%s evaluated at %f = %f\n", expression_string, x, te_eval(expr));
    
    te_expr* copy = te_expr_deep_copy(expr);
    printf("%s'copy evaluated at %f = %f\n", expression_string, x, te_eval(copy));
    
    te_expr* derivative = te_differentiate_symbolically(expr, &var, &error);
    printf("%s's derivative evaluated at %f = %f\n", expression_string, x, te_eval(derivative));

    return 0;
}
