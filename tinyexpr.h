// SPDX-License-Identifier: Zlib
/*
 * TINYEXPR - Tiny recursive descent parser and evaluation engine in C
 *
 * Copyright (c) 2015-2020 Lewis Van Winkle
 *
 * http://CodePlea.com
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software
 * in a product, an acknowledgement in the product documentation would be
 * appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef TINYEXPR_H
#define TINYEXPR_H


#ifdef __cplusplus
extern "C" {
#endif

    /* Private */
    typedef double (*te_fun0)(void);
    typedef double (*te_fun1)(double);
    typedef double (*te_fun2)(double, double);
    typedef double (*te_fun3)(double, double, double);
    typedef double (*te_fun4)(double, double, double, double);
    typedef double (*te_fun5)(double, double, double, double, double);
    typedef double (*te_fun6)(double, double, double, double, double, double);
    typedef double (*te_fun7)(double, double, double, double, double, double, double);

    typedef double (*te_clo0)(void* context);
    typedef double (*te_clo1)(void* context, double a);
    typedef double (*te_clo2)(void* context, double a, double b);
    typedef double (*te_clo3)(void* context, double a, double b, double c);
    typedef double (*te_clo4)(void* context, double a, double b, double c, double d);
    typedef double (*te_clo5)(void* context, double a, double b, double c, double d, double e);
    typedef double (*te_clo6)(void* context, double a, double b, double c, double d, double e, double f);
    typedef double (*te_clo7)(void* context, double a, double b, double c, double d, double e, double f, double g);

    /* Public */
    typedef struct te_expr {
        union {
            double value;
            const double *bound;
            te_fun0 fun0;
            te_fun1 fun1;
            te_fun2 fun2;
            te_fun3 fun3;
            te_fun4 fun4;
            te_fun5 fun5;
            te_fun6 fun6;
            te_fun7 fun7;
            te_clo0 clo0;
            te_clo1 clo1;
            te_clo2 clo2;
            te_clo3 clo3;
            te_clo4 clo4;
            te_clo5 clo5;
            te_clo6 clo6;
            te_clo7 clo7;
        } expr;
        int type;
        struct te_expr *parameters[1];
    } te_expr;


    enum {
        TE_VARIABLE = 0,

        TE_FUNCTION0 = 8, TE_FUNCTION1, TE_FUNCTION2, TE_FUNCTION3,
        TE_FUNCTION4, TE_FUNCTION5, TE_FUNCTION6, TE_FUNCTION7,

        TE_CLOSURE0 = 16, TE_CLOSURE1, TE_CLOSURE2, TE_CLOSURE3,
        TE_CLOSURE4, TE_CLOSURE5, TE_CLOSURE6, TE_CLOSURE7,

        TE_FLAG_PURE = 32
    };

    typedef struct te_variable {
        const char *name;
        union {
            const double *address;
            double* variable;
            te_fun0 fun0;
            te_fun1 fun1;
            te_fun2 fun2;
            te_fun3 fun3;
            te_fun4 fun4;
            te_fun5 fun5;
            te_fun6 fun6;
            te_fun7 fun7;
            te_clo0 clo0;
            te_clo1 clo1;
            te_clo2 clo2;
            te_clo3 clo3;
            te_clo4 clo4;
            te_clo5 clo5;
            te_clo6 clo6;
            te_clo7 clo7;
        } el;
        int type;
        te_expr *context;
    } te_variable;



    /* Parses the input expression, evaluates it, and frees it. */
    /* Returns NaN on error. */
    double te_interp(const char *expression, int *error);

    /* Parses the input expression and binds variables. */
    /* Returns NULL on error. */
    te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error);

    /* Evaluates the expression. */
    double te_eval(const te_expr *n);

    /* Prints debugging information on the syntax tree. */
    void te_print(const te_expr *n);

    /* Frees the expression. */
    /* This is safe to call on NULL pointers. */
    void te_free(te_expr *n);


#ifdef __cplusplus
}
#endif

#endif /*TINYEXPR_H*/
