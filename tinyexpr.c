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

/* Exponentiation associativity:
   a**b**c = a**(b**c) and -a**b = -(a**b)
*/

#include "tinyexpr.h"

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif


enum {
    TOK_NULL = TE_CLOSURE7+1, TOK_ERROR, TOK_END, TOK_SEP,
    TOK_OPEN, TOK_CLOSE, TOK_NUMBER, TOK_VARIABLE, TOK_INFIX
};


enum {TE_CONSTANT = 1};


typedef struct state {
    const char *start;
    const char *next;
    int type;
    union {double value; const double *bound; te_fun0 fun0; te_fun1 fun1; te_fun2 fun2;} expr;
    te_expr *context;

    const te_variable *lookup;
    int lookup_len;
} state;


#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION0) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE0) != 0)
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION0 | TE_CLOSURE0)) ? ((TYPE) & 0x00000007) : 0 )
#ifdef __cplusplus
#include <initializer_list>
#define NEW_EXPR(type, ...) new_expr((type), &*std::initializer_list<const te_expr *>({__VA_ARGS__}).begin())
#else
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})
#endif
#define CHECK_NULL(ptr, ...) if ((ptr) == NULL) { __VA_ARGS__; return NULL; }

static te_expr *new_expr(const int type, const te_expr * const parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr *ret = (te_expr *) malloc(size);
    CHECK_NULL(ret);

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->expr.bound = 0;
    return ret;
}


void te_free_parameters(te_expr *n) {
    if (!n) return;
    switch (TYPE_MASK(n->type)) {
        case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);     /* Falls through. */
        case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);     /* Falls through. */
        case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);     /* Falls through. */
        case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);     /* Falls through. */
        case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);     /* Falls through. */
        case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);     /* Falls through. */
        case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    }
}


void te_free(te_expr *n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi(void) {return 3.14159265358979323846;}
static double e(void) {return 2.71828182845904523536;}

static double fac(double a) {
    return tgamma(a + 1);
}

static double ncr(double n, double r) { // combinations
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;
    unsigned long int un = (unsigned int)(n), ur = (unsigned int)(r), i;
    unsigned long int result = 1;
    if (ur > un / 2) ur = un - ur;
    for (i = 1; i <= ur; i++) {
        if (result > ULONG_MAX / (un - ur + i))
            return INFINITY;
        result *= un - ur + i;
        result /= i;
    }
    return result;
}

static double npr(double n, double r) { // permutations
    return ncr(n, r) * fac(r);
}

typedef unsigned long long te_ull;

static double gcd(double x, double y) {
    unsigned long long a = (unsigned int)(x), b = (unsigned int)(y), r;
    while (b > 0) {
        r = a % b;
        a = b;
        b = r;
    }
    return (double) a;
}

/*
warning C4232: nonstandard extension used: 'fun1': address of dllimport 'cbrt' is not static, identity not guaranteed
2>C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt\corecrt_math.h(493): message : see declaration of 'cbrt'
warning C4232: nonstandard extension used: 'fun1': address of dllimport 'tgamma' is not static, identity not guaranteed
2>C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt\corecrt_math.h(534): message : see declaration of 'tgamma'
warning C4232: nonstandard extension used: 'fun1': address of dllimport 'log2' is not static, identity not guaranteed
2>C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0\ucrt\corecrt_math.h(516): message : see declaration of 'log2'
*/
static double sd_ceil(double n) { return ceil(n); }
static double sd_floor(double n) { return floor(n); }
static double sd_cbrt(double n) { return cbrt(n); }
static double sd_tgamma(double n) { return tgamma(n); }
static double sd_log2(double n) { return log2(n); }

#ifdef _MSC_VER
#pragma function (ceil)
#pragma function (floor)
#endif

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {.name="abs",   .el.fun1=fabs,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="acos",  .el.fun1=acos,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="asin",  .el.fun1=asin,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="atan",  .el.fun1=atan,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="atan2", .el.fun2=atan2, .type=TE_FUNCTION2 | TE_FLAG_PURE, .context=0},
    {.name="cbrt",  .el.fun1=sd_cbrt,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="ceil",  .el.fun1=sd_ceil,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="cos",   .el.fun1=cos,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="cosh",  .el.fun1=cosh,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="e",     .el.fun0=e,     .type=TE_FUNCTION0 | TE_FLAG_PURE, .context=0},
    {.name="exp",   .el.fun1=exp,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="fac",   .el.fun1=fac,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="floor", .el.fun1=sd_floor, .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="gamma", .el.fun1=sd_tgamma,.type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="gcd",   .el.fun2=gcd,   .type=TE_FUNCTION2 | TE_FLAG_PURE, .context=0},
    {.name="log",   .el.fun1=log,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="log10", .el.fun1=log10, .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="log2",  .el.fun1=sd_log2,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="ncr",   .el.fun2=ncr,   .type=TE_FUNCTION2 | TE_FLAG_PURE, .context=0},
    {.name="npr",   .el.fun2=npr,   .type=TE_FUNCTION2 | TE_FLAG_PURE, .context=0},
    {.name="pi",    .el.fun0=pi,    .type=TE_FUNCTION0 | TE_FLAG_PURE, .context=0},
    {.name="pow",   .el.fun2=pow,   .type=TE_FUNCTION2 | TE_FLAG_PURE, .context=0},
    {.name="sin",   .el.fun1=sin,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="sinh",  .el.fun1=sinh,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="sqrt",  .el.fun1=sqrt,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="tan",   .el.fun1=tan,   .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {.name="tanh",  .el.fun1=tanh,  .type=TE_FUNCTION1 | TE_FLAG_PURE, .context=0},
    {0, 0, 0, 0}
};

static const te_variable *find_builtin(const char *name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax-imin)/2));
        int c = strncmp(name, functions[i].name, len);
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        } else if (c > 0) {
            imin = i + 1;
        } else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) {
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}



static double add(double a, double b) {return a + b;}
static double sub(double a, double b) {return a - b;}
static double mul(double a, double b) {return a * b;}
static double divide(double a, double b) {return a / b;}
static double negate(double a) {return -a;}
static double comma(double a, double b) {(void)a; return b;}

static double greater(double a, double b) {return a > b;}
static double greater_eq(double a, double b) {return a >= b;}
static double lower(double a, double b) {return a < b;}
static double lower_eq(double a, double b) {return a <= b;}
static double equal(double a, double b) {return a == b;}
static double not_equal(double a, double b) {return a != b;}
static double logical_and(double a, double b) {return a != 0.0 && b != 0.0;}
static double logical_or(double a, double b) {return a != 0.0 || b != 0.0;}
static double logical_not(double a) {return a == 0.0;}
static double logical_notnot(double a) {return a != 0.0;}
static double negate_logical_not(double a) {return -(a == 0.0);}
static double negate_logical_notnot(double a) {return -(a != 0.0);}
static double shift_left(double a, double b) {return llround(a) << llround(b);}
static double shift_right(double a, double b) {return llround(a) >> llround(b);}
static double bitwise_and(double a, double b) {return llround(a) & llround(b);}
static double bitwise_or(double a, double b) {return llround(a) | llround(b);}
static double bitwise_xor(double a, double b) {return llround(a) ^ llround(b);}
// TODO static double bitwise_not(double a) {return ~llround(a) & 0x1FFFFFFFFFFFFFLL;}


void next_token(state *s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if ((s->next[0] >= '0' && s->next[0] <= '9') || s->next[0] == '.') {
            s->expr.value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } else {
            /* Look for a variable or builtin function call. */
            if (isalpha(s->next[0])) {
                const char *start;
                start = s->next;
                while (isalpha(s->next[0]) || isdigit(s->next[0]) || (s->next[0] == '_')) s->next++;
                
                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
                    s->type = TOK_ERROR;
                } else {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->expr.bound = var->el.variable;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->expr.fun1 = var->el.fun1;
                            break;
                    }
                }

            } else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                    case '+': s->type = TOK_INFIX; s->expr.fun2 = add; break;
                    case '-': s->type = TOK_INFIX; s->expr.fun2 = sub; break;
                    case '*':
                        if (*s->next=='*') {++s->next; s->expr.fun2 = pow;}
                        else s->expr.fun2 = mul;
                        s->type = TOK_INFIX; break;
                    case '/': s->type = TOK_INFIX; s->expr.fun2 = divide; break;
                    case '%': s->type = TOK_INFIX; s->expr.fun2 = fmod; break;
                    case '!':
                        if (s->next++[0] == '=') {
                            s->type = TOK_INFIX; s->function = not_equal;
                        } else {
                            s->next--;
                            s->type = TOK_INFIX; s->function = logical_not;
                        }
                        break;
                    case '=':
                        if (s->next++[0] == '=') {
                            s->type = TOK_INFIX; s->function = equal;
                        } else {
                            s->type = TOK_ERROR;
                        }
                        break;
                    case '<':
                        if (s->next[0] == '=') {
                            s->next++;
                            s->type = TOK_INFIX; s->function = lower_eq;
                        } else if (s->next[0] == '<') {
                            s->next++;
                            s->type = TOK_INFIX; s->function = shift_left;
                        } else {
                            s->type = TOK_INFIX; s->function = lower;
                        }
                        break;
                    case '>':
                        if (s->next[0] == '=') {
                            s->next++;
                            s->type = TOK_INFIX; s->function = greater_eq;
                        } else if (s->next[0] == '>') {
                            s->next++;
                            s->type = TOK_INFIX; s->function = shift_right;
                        } else {
                            s->type = TOK_INFIX; s->function = greater;
                        }
                        break;
                    case '&':
                        if (s->next++[0] == '&') {
                            s->type = TOK_INFIX; s->function = logical_and;
                        } else {
                            s->next--;
                            s->type = TOK_INFIX; s->function = bitwise_and;
                        }
                        break;
                    case '|':
                        if (s->next++[0] == '|') {
                            s->type = TOK_INFIX; s->function = logical_or;
                        } else {
                            s->next--;
                            s->type = TOK_INFIX; s->function = bitwise_or;
                        }
                        break;
                    case '^': s->type = TOK_INFIX; s->function = bitwise_xor; break;
                    // TODO case '~': s->type = TOK_INFIX; s->function = bitwise_not; break;
                    case '(': s->type = TOK_OPEN; break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
            }
        }
    } while (s->type == TOK_NULL);
}


static te_expr *list(state *s);
static te_expr *expr(state *s);
static te_expr *power(state *s);

static te_expr *base(state *s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *ret;
    int arity;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            ret = new_expr(TE_CONSTANT, 0);
            CHECK_NULL(ret);

            ret->expr.value = s->expr.value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            ret = new_expr(TE_VARIABLE, 0);
            CHECK_NULL(ret);

            ret->expr.bound = s->expr.bound;
            next_token(s);
            break;

        case TE_FUNCTION0:
        case TE_CLOSURE0:
            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->expr.fun0 = s->expr.fun0;
            if (IS_CLOSURE(s->type)) ret->parameters[0] = s->context;
            next_token(s);
            if (s->type == TOK_OPEN) {
                next_token(s);
                if (s->type != TOK_CLOSE) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }
            break;

        case TE_FUNCTION1:
        case TE_CLOSURE1:
            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->expr.fun1 = s->expr.fun1;
            if (IS_CLOSURE(s->type)) ret->parameters[1] = s->context;
            next_token(s);
            ret->parameters[0] = power(s);
            CHECK_NULL(ret->parameters[0], te_free(ret));
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            arity = ARITY(s->type);

            ret = new_expr(s->type, 0);
            CHECK_NULL(ret);

            ret->expr.fun2 = s->expr.fun2;
            if (IS_CLOSURE(s->type)) ret->parameters[arity] = s->context;
            next_token(s);

            if (s->type != TOK_OPEN) {
                s->type = TOK_ERROR;
            } else {
                int i;
                for(i = 0; i < arity; i++) {
                    next_token(s);
                    ret->parameters[i] = expr(s);
                    CHECK_NULL(ret->parameters[i], te_free(ret));

                    if(s->type != TOK_SEP) {
                        break;
                    }
                }
                if(s->type != TOK_CLOSE || i != arity - 1) {
                    s->type = TOK_ERROR;
                } else {
                    next_token(s);
                }
            }

            break;

        case TOK_OPEN:
            next_token(s);
            ret = list(s);
            CHECK_NULL(ret);

            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            } else {
                next_token(s);
            }
            break;

        default:
            ret = new_expr(0, 0);
            CHECK_NULL(ret);

            s->type = TOK_ERROR;
            ret->expr.value = NAN;
            break;
    }

    return ret;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+" | "!")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->expr.fun2 == add || s->expr.fun2 == sub)) {
        if (s->expr.fun2 == sub) sign = -sign;
        next_token(s);
    }

    int logical = 0;
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub || s->function == logical_not)) {
        if (s->function == logical_not) {
            if (logical == 0) {
                logical = -1;
            } else {
                logical = -logical;
            }
        }
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) {
        if (logical == 0) {
            ret = base(s);
        } else if (logical == -1) {
            ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
            ret->function = logical_not;
        } else {
            ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
            ret->function = logical_notnot;
        }
    } else {
        te_expr *b = base(s);
        CHECK_NULL(b);

        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, b);
        CHECK_NULL(ret, te_free(b));

        if (logical == 0) {
            ret->expr.fun1 = negate;
        } else if (logical == -1) {
            ret->function = negate_logical_not;
        } else {
            ret->function = negate_logical_notnot;
        }
    }

    return ret;
}

static te_expr *factor(state *s) {
    /* <factor>    =    <power> {"**" <power>} */
    te_expr *ret = power(s);
    CHECK_NULL(ret);

    const void *left_function = NULL;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) &&
        (ret->function == negate || ret->function == logical_not || ret->function == logical_notnot ||
        ret->function == negate_logical_not || ret->function == negate_logical_notnot)) {
        left_function = ret->function;
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
    }

    te_expr *insertion = 0;
    te_fun2 dpow = pow; /* resolve overloading for g++ */
    while (s->type == TOK_INFIX && (s->expr.fun2 == dpow)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr *p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr *insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], p);
            CHECK_NULL(insert, te_free(p), te_free(ret));

            insert->expr.fun2 = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        } else {
            te_expr *p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr *prev = ret;
            ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
            CHECK_NULL(ret, te_free(p), te_free(prev));

            ret->expr.fun2 = t;
            insertion = ret;
        }
    }

    if (left_function) {
        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        ret->function = left_function;
        CHECK_NULL(ret, te_free(prev));

    }

    return ret;
}

static te_expr *term(state *s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);
    CHECK_NULL(ret);
    te_fun2 dmod = fmod; /* resolve c++ overloading */
    while (s->type == TOK_INFIX && (s->expr.fun2 == mul || s->expr.fun2 == divide || s->expr.fun2 == dmod)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        te_expr *f = factor(s);
        CHECK_NULL(f, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, f);
        CHECK_NULL(ret, te_free(f), te_free(prev));

        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr *sum_expr(state *s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->expr.fun2 == add || s->expr.fun2 == sub)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        te_expr *te = term(s);
        CHECK_NULL(te, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, te);
        CHECK_NULL(ret, te_free(te), te_free(prev));

        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr *shift_expr(state *s) {
    /* <expr>      =    <sum_expr> {("<<" | ">>") <sum_expr>} */
    te_expr *ret = sum_expr(s);

    while (s->type == TOK_INFIX && (s->function == shift_left || s->function == shift_right)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, sum_expr(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *test_expr(state *s) {
    /* <expr>      =    <shift_expr> {(">" | ">=" | "<" | "<=" | "==" | "!=") <shift_expr>} */
    te_expr *ret = shift_expr(s);

    while (s->type == TOK_INFIX && (s->function == greater || s->function == greater_eq ||
        s->function == lower || s->function == lower_eq || s->function == equal || s->function == not_equal)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, shift_expr(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *bitw_expr(state *s) {
    /* <expr>      =    <test_expr> {("&" | "|" | "^") <test_expr>} */
    te_expr *ret = test_expr(s);

    while (s->type == TOK_INFIX && (s->function == bitwise_and || s->function == bitwise_or || s->function == bitwise_xor)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, test_expr(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *expr(state *s) {
    /* <expr>      =    <bitw_expr> {("&&" | "||") <bitw_expr>} */
    te_expr *ret = bitw_expr(s);

    while (s->type == TOK_INFIX && (s->function == logical_and || s->function == logical_or)) {
        te_fun2 t = s->function;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, bitw_expr(s));
        ret->function = t;
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_SEP) {
        next_token(s);
        te_expr *e = expr(s);
        CHECK_NULL(e, te_free(ret));

        te_expr *prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, e);
        CHECK_NULL(ret, te_free(e), te_free(prev));

        ret->expr.fun2 = comma;
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->expr.fun1)
#define M(e) te_eval(n->parameters[e])
#define D(e) double
#define TE_R1(x) x(0)
#define TE_R2(x) TE_R1(x), x(1)
#define TE_R3(x) TE_R2(x), x(2)
#define TE_R4(x) TE_R3(x), x(3)
#define TE_R5(x) TE_R4(x), x(4)
#define TE_R6(x) TE_R5(x), x(5)
#define TE_R7(x) TE_R6(x), x(6)


double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->expr.value;
        case TE_VARIABLE: return *n->expr.bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return n->expr.fun0();
                case 1: return n->expr.fun1( M(0) );
                case 2: return n->expr.fun2( M(0), M(1) );
                case 3: return TE_FUN(TE_R3(D))( TE_R3(M) );
                case 4: return TE_FUN(TE_R4(D))( TE_R4(M) );
                case 5: return TE_FUN(TE_R5(D))( TE_R5(M) );
                case 6: return TE_FUN(TE_R6(D))( TE_R6(M) );
                case 7: return TE_FUN(TE_R7(D))( TE_R7(M) );
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return ((double(*)(te_expr*))n->expr.fun1)( n->parameters[0] );
                case 1: return TE_FUN(te_expr*, TE_R1(D))( n->parameters[1], TE_R1(M) );
                case 2: return TE_FUN(te_expr*, TE_R2(D))( n->parameters[2], TE_R2(M) );
                case 3: return TE_FUN(te_expr*, TE_R3(D))( n->parameters[3], TE_R3(M) );
                case 4: return TE_FUN(te_expr*, TE_R4(D))( n->parameters[4], TE_R4(M) );
                case 5: return TE_FUN(te_expr*, TE_R5(D))( n->parameters[5], TE_R5(M) );
                case 6: return TE_FUN(te_expr*, TE_R6(D))( n->parameters[6], TE_R6(M) );
                case 7: return TE_FUN(te_expr*, TE_R7(D))( n->parameters[7], TE_R7(M) );
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef D
#undef M

static void optimize(te_expr *n) {
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) {
                known = 0;
            }
        }
        if (known) {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->expr.value = value;
        }
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);
    if (root == NULL) {
        if (error) *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        te_free(root);
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } else {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}


double te_interp(const char *expression, int *error) {
    te_expr *n = te_compile(expression, 0, 0, error);
    if (n == NULL) {
        return NAN;
    }

    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    } else {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->expr.value); break;
    case TE_VARIABLE: printf("bound %p\n", n->expr.bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
         arity = ARITY(n->type);
         printf("f%d", arity);
         for(i = 0; i < arity; i++) {
             printf(" %p", n->parameters[i]);
         }
         printf("\n");
         for(i = 0; i < arity; i++) {
             pn(n->parameters[i], depth + 1);
         }
         break;
    }
}


void te_print(const te_expr *n) {
    pn(n, 0);
}
