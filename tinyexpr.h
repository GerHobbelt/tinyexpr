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

 /* Exponentiation associativity:
 For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
 For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.
 */
 #define TE_POW_FROM_RIGHT 1

 /* Logarithms:
 For log = base 10 log do nothing
 For log = natural log uncomment the next line.
 */
 /* #define TE_NAT_LOG 1 */
#undef TE_NAT_LOG


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


	typedef enum te_type
	{
		TE_VARIABLE = 0,
		TE_CONSTANT = 1,

		TOK_NULL, TOK_ERROR, TOK_END, TOK_SEP,
		TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE,
		TOK_INFIX,

		TE_FUNCTION = 16, TE_FUNCTION0 = TE_FUNCTION, TE_FUNCTION1, TE_FUNCTION2, TE_FUNCTION3,
		TE_FUNCTION4, TE_FUNCTION5, TE_FUNCTION6, TE_FUNCTION7, TE_FUNCTION_VA,

		TE_CLOSURE = 32, TE_CLOSURE0 = TE_CLOSURE, TE_CLOSURE1, TE_CLOSURE2, TE_CLOSURE3,
		TE_CLOSURE4, TE_CLOSURE5, TE_CLOSURE6, TE_CLOSURE7, TE_CLOSURE_VA,

		TE_FUNCTION_TYPE_MASK = 63,    // 64 - 1 
		TE_FUNCTION_ARITY_MASK = 15,   // 16 - 1

		TE_FLAG_PURE = 64,
	} te_type;


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
        te_type type;
        struct te_expr *parameters[1]; // https://stackoverflow.com/a/4413035/9731532
    } te_expr;


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
        te_type type;
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
