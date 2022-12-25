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

 /* COMPILE TIME OPTIONS */

 /* Exponentiation associativity:
 For a^b^c = (a^b)^c and -a^b = (-a)^b do nothing.
 For a^b^c = a^(b^c) and -a^b = -(a^b) uncomment the next line.*/
 /* #define TE_POW_FROM_RIGHT */

 /* Logarithms
 For log = base 10 log do nothing
 For log = natural log uncomment the next line. */
 /* #define TE_NAT_LOG */

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


typedef struct state {
    const char* start;
    const char* next;
    te_type type;
    union {
        double value;
        const double* bound;
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
    te_expr* context;

    const te_variable* lookup;
    int lookup_len;
} state;


// If it is a variable, the byte is 0
// If it's a constant, the byte is 1 (0000 0001)
//
// If it's a function, the byte indicates the number of parameters 
// 000_01_XXX -- 01 is func, XXX is num_params
// (Underscores indicate spaces)
//
// Same story with the closure.
// 000_10_XXX -- 10 is closure, XXX is num_params
//
// FLAG_PURE obviously means whether it modifies the parameters 
// (or the captured data, in case of closures)
// It is set as the 6th bit.
// 00_1_-----, where ----- means any 5 bits.
// 
// This means the type occupies only the last 6 bits. 
// (that is, if you count the pure flag a part of the type)
// For this macro, that flag is neglected.
#define TYPE_MASK(TYPE) ((TYPE)&0x0000001F)

#define IS_PURE(TYPE) (((TYPE) & TE_FLAG_PURE) != 0)
#define IS_FUNCTION(TYPE) (((TYPE) & TE_FUNCTION) != 0)
#define IS_CLOSURE(TYPE) (((TYPE) & TE_CLOSURE) != 0)

// Returns the number of arguments the function takes. 
// If the input is not a function, the result is 0.
// The number of arguments is determined by examining the flags
#define ARITY(TYPE) ( ((TYPE) & (TE_FUNCTION | TE_CLOSURE)) ? ((TYPE) & 0x00000007) : 0 )
#ifdef __cplusplus
#include <initializer_list>
#define NEW_EXPR(type, ...) new_expr((type), &*std::initializer_list<const te_expr *>({__VA_ARGS__}).begin())
#else
#define NEW_EXPR(type, ...) new_expr((type), (const te_expr*[]){__VA_ARGS__})
#endif
#define CHECK_NULL(ptr, ...) if ((ptr) == NULL) { __VA_ARGS__; return NULL; }

static te_expr* new_expr(const int type, const te_expr* const parameters[]) {
    const int arity = ARITY(type);
    const int psize = sizeof(void*) * arity;
    const int size = (sizeof(te_expr) - sizeof(void*)) + psize + (IS_CLOSURE(type) ? sizeof(void*) : 0);
    te_expr* ret = (te_expr*)malloc(size);
    CHECK_NULL(ret);

    memset(ret, 0, size);
    if (arity && parameters) {
        memcpy(ret->parameters, parameters, psize);
    }
    ret->type = type;
    ret->expr.bound = 0;
    return ret;
}


void te_free_parameters(te_expr* n) {
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


void te_free(te_expr* n) {
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi(void) { return 3.14159265358979323846; }
static double e(void) { return 2.71828182845904523536; }

static double fac(double a) {
    // tweak to ensure factorial(-1) -> NaN instead of +INF, which tgamma(a+1) would deliver cf. C std spec.
    if (a > 0)
        return tgamma(a + 1);
    return NAN;
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
    return (double)a;
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
static double sd_min(double c, double d) {return (c < d) ? c : d;}
static double sd_max(double c, double d) {return (c > d) ? c : d;}

static double sd_ln(double n) { return log(n); }
static double sd_log10(double n) { return log10(n); }

#ifdef _MSC_VER
#pragma function (ceil)
#pragma function (floor)
#endif

/*
    _Check_return_ double __cdecl acos(_In_ double _X);
    _Check_return_ double __cdecl asin(_In_ double _X);
    _Check_return_ double __cdecl atan(_In_ double _X);
    _Check_return_ double __cdecl atan2(_In_ double _Y, _In_ double _X);

    _Check_return_ double __cdecl cos(_In_ double _X);
    _Check_return_ double __cdecl cosh(_In_ double _X);
    _Check_return_ double __cdecl exp(_In_ double _X);
    _Check_return_ _CRT_JIT_INTRINSIC double __cdecl fabs(_In_ double _X);
    _Check_return_ double __cdecl fmod(_In_ double _X, _In_ double _Y);
    _Check_return_ double __cdecl log(_In_ double _X);
    _Check_return_ double __cdecl log10(_In_ double _X);
    _Check_return_ double __cdecl pow(_In_ double _X, _In_ double _Y);
    _Check_return_ double __cdecl sin(_In_ double _X);
    _Check_return_ double __cdecl sinh(_In_ double _X);
    _Check_return_ _CRT_JIT_INTRINSIC double __cdecl sqrt(_In_ double _X);
    _Check_return_ double __cdecl tan(_In_ double _X);
    _Check_return_ double __cdecl tanh(_In_ double _X);

    _Check_return_ _ACRTIMP double    __cdecl acosh(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl asinh(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl atanh(_In_ double _X);
    _Check_return_ _ACRTIMP  double    __cdecl atof(_In_z_ char const* _String);
    _Check_return_ _ACRTIMP  double    __cdecl _atof_l(_In_z_ char const* _String, _In_opt_ _locale_t _Locale);
    _Check_return_ _ACRTIMP double    __cdecl _cabs(_In_ struct _complex _Complex_value);
    _Check_return_ _ACRTIMP double    __cdecl cbrt(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl ceil(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl _chgsign(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl copysign(_In_ double _Number, _In_ double _Sign);
    _Check_return_ _ACRTIMP double    __cdecl _copysign(_In_ double _Number, _In_ double _Sign);
    _Check_return_ _ACRTIMP double    __cdecl erf(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl erfc(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl exp2(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl expm1(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl fdim(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double    __cdecl floor(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl fma(_In_ double _X, _In_ double _Y, _In_ double _Z);
    _Check_return_ _ACRTIMP double    __cdecl fmax(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double    __cdecl fmin(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double    __cdecl frexp(_In_ double _X, _Out_ int* _Y);
    _Check_return_ _ACRTIMP double    __cdecl hypot(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double    __cdecl _hypot(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP int       __cdecl ilogb(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl ldexp(_In_ double _X, _In_ int _Y);
    _Check_return_ _ACRTIMP double    __cdecl lgamma(_In_ double _X);
    _Check_return_ _ACRTIMP long long __cdecl llrint(_In_ double _X);
    _Check_return_ _ACRTIMP long long __cdecl llround(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl log1p(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl log2(_In_ double _X);
    _Check_return_ _ACRTIMP double    __cdecl logb(_In_ double _X);
    _Check_return_ _ACRTIMP long      __cdecl lrint(_In_ double _X);
    _Check_return_ _ACRTIMP long      __cdecl lround(_In_ double _X);

    int __CRTDECL _matherr(_Inout_ struct _exception* _Except);

    _Check_return_ _ACRTIMP double __cdecl modf(_In_ double _X, _Out_ double* _Y);
    _Check_return_ _ACRTIMP double __cdecl nan(_In_ char const* _X);
    _Check_return_ _ACRTIMP double __cdecl nearbyint(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl nextafter(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double __cdecl nexttoward(_In_ double _X, _In_ long double _Y);
    _Check_return_ _ACRTIMP double __cdecl remainder(_In_ double _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double __cdecl remquo(_In_ double _X, _In_ double _Y, _Out_ int* _Z);
    _Check_return_ _ACRTIMP double __cdecl rint(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl round(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl scalbln(_In_ double _X, _In_ long _Y);
    _Check_return_ _ACRTIMP double __cdecl scalbn(_In_ double _X, _In_ int _Y);
    _Check_return_ _ACRTIMP double __cdecl tgamma(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl trunc(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl _j0(_In_ double _X );
    _Check_return_ _ACRTIMP double __cdecl _j1(_In_ double _X );
    _Check_return_ _ACRTIMP double __cdecl _jn(int _X, _In_ double _Y);
    _Check_return_ _ACRTIMP double __cdecl _y0(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl _y1(_In_ double _X);
    _Check_return_ _ACRTIMP double __cdecl _yn(_In_ int _X, _In_ double _Y);
*/

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {.name = "abs",   .el.fun1 = fabs,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "acos",  .el.fun1 = acos,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "asin",  .el.fun1 = asin,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "atan",  .el.fun1 = atan,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "atan2", .el.fun2 = atan2, .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "cbrt",  .el.fun1 = sd_cbrt,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "ceil",  .el.fun1 = sd_ceil,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "cos",   .el.fun1 = cos,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "cosh",  .el.fun1 = cosh,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "e",     .el.fun0 = e,     .type = TE_FUNCTION0 | TE_FLAG_PURE, .context = 0},
    {.name = "exp",   .el.fun1 = exp,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "fac",   .el.fun1 = fac,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "floor", .el.fun1 = sd_floor, .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "gamma", .el.fun1 = sd_tgamma,.type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "gcd",   .el.fun2 = gcd,   .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "ln",    .el.fun1 = sd_ln, .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
#ifdef TE_NAT_LOG
    {.name = "log",   .el.fun1 = sd_ln,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
#else
    {.name = "log",   .el.fun1 = sd_log10,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
#endif
    {.name = "log10", .el.fun1 = sd_log10, .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "log2",  .el.fun1 = sd_log2,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "max", .el.fun2 = sd_max,      .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "min", .el.fun2 = sd_min,      .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "ncr",   .el.fun2 = ncr,   .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "npr",   .el.fun2 = npr,   .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "pi",    .el.fun0 = pi,    .type = TE_FUNCTION0 | TE_FLAG_PURE, .context = 0},
    {.name = "pow",   .el.fun2 = pow,   .type = TE_FUNCTION2 | TE_FLAG_PURE, .context = 0},
    {.name = "sin",   .el.fun1 = sin,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "sinh",  .el.fun1 = sinh,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "sqrt",  .el.fun1 = sqrt,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "tan",   .el.fun1 = tan,   .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {.name = "tanh",  .el.fun1 = tanh,  .type = TE_FUNCTION1 | TE_FLAG_PURE, .context = 0},
    {0, 0, 0, 0}
};

static const te_variable* find_builtin(const char* name, int len) {
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /*Binary search.*/
    while (imax >= imin) {
        const int i = (imin + ((imax - imin) / 2));
        int c = strncmp(name, functions[i].name, len);

		// In case the lengths are the same, verify if they both null terminate:
		// f.e. consider searching for `sin`: `sinh` would match too.
        if (!c) c = '\0' - functions[i].name[len];
        if (c == 0) {
            return functions + i;
        }
        else if (c > 0) {
            imin = i + 1;
        }
        else {
            imax = i - 1;
        }
    }

    return 0;
}

static const te_variable* find_lookup(const state* s, const char* name, int len) {
    int iters;
    const te_variable* var;
    if (!s->lookup) return 0;

	// Does a linear search. Could keep a sorted tree for these, maybe.
	// It doesn't really matter, since few variables will be defined, probably.
    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') {
            return var;
        }
    }
    return 0;
}



static double add(double a, double b) { return a + b; }
static double sub(double a, double b) { return a - b; }
static double mul(double a, double b) { return a * b; }
static double divide(double a, double b) { return a / b; }
static double negate(double a) { return -a; }
static double comma(double a, double b) { (void)a; return b; }

static double greater(double a, double b) { return a > b; }
static double greater_eq(double a, double b) { return a >= b; }
static double lower(double a, double b) { return a < b; }
static double lower_eq(double a, double b) { return a <= b; }
static double equal(double a, double b) { return a == b; }
static double not_equal(double a, double b) { return a != b; }
static double logical_and(double a, double b) { return a != 0.0 && b != 0.0; }
static double logical_or(double a, double b) { return a != 0.0 || b != 0.0; }
static double logical_not(double a) { return a == 0.0; }
static double logical_notnot(double a) { return a != 0.0; }
static double negate_logical_not(double a) { return -(a == 0.0); }
static double negate_logical_notnot(double a) { return -(a != 0.0); }
static double shift_left(double a, double b) { return llround(a) << llround(b); }
static double shift_right(double a, double b) { return llround(a) >> llround(b); }
static double bitwise_and(double a, double b) { return llround(a) & llround(b); }
static double bitwise_or(double a, double b) { return llround(a) | llround(b); }
static double bitwise_xor(double a, double b) { return llround(a) ^ llround(b); }
static double bitwise_not(double a) {
	return ~llround(a) & 0x1FFFFFFFFFFFFFLL;
}

void next_token(state* s) {
    s->type = TOK_NULL;

    do {

        if (!*s->next) {
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if (isdigit(s->next[0]) || s->next[0] == '.') {
            s->expr.value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        }
        else {
            /* Look for a variable or builtin function call. */
            if (isalpha(s->next[0])) {
                const char* start;
                start = s->next;
                while (isalpha(s->next[0]) || isdigit(s->next[0]) || (s->next[0] == '_')) s->next++;

                const te_variable* var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var) {
					// TODO: Maybe add better errors?
                    s->type = TOK_ERROR;
                }
                else {
                    switch (TYPE_MASK(var->type))
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

            }
            else {
                /* Look for an operator or special character. */
                switch (s->next++[0]) {
                case '+': s->type = TOK_INFIX; s->expr.fun2 = add; break;
                case '-': s->type = TOK_INFIX; s->expr.fun2 = sub; break;
                case '*':
                    if (*s->next == '*') { ++s->next; s->expr.fun2 = pow; }
                    else s->expr.fun2 = mul;
                    s->type = TOK_INFIX; break;
                case '/': s->type = TOK_INFIX; s->expr.fun2 = divide; break;
                case '%': s->type = TOK_INFIX; s->expr.fun2 = fmod; break;
                case '!':
                    if (s->next++[0] == '=') {
                        s->type = TOK_INFIX; s->expr.fun2 = not_equal;
                    }
                    else {
                        s->next--;
                        s->type = TOK_INFIX; s->expr.fun1 = logical_not;
                    }
                    break;
                case '=':
                    if (s->next++[0] == '=') {
                        s->type = TOK_INFIX; s->expr.fun2 = equal;
                    }
                    else {
                        s->type = TOK_ERROR;
                    }
                    break;
                case '<':
                    if (s->next[0] == '=') {
                        s->next++;
                        s->type = TOK_INFIX; s->expr.fun2 = lower_eq;
                    }
                    else if (s->next[0] == '<') {
                        s->next++;
                        s->type = TOK_INFIX; s->expr.fun2 = shift_left;
                    }
                    else {
                        s->type = TOK_INFIX; s->expr.fun2 = lower;
                    }
                    break;
                case '>':
                    if (s->next[0] == '=') {
                        s->next++;
                        s->type = TOK_INFIX; s->expr.fun2 = greater_eq;
                    }
                    else if (s->next[0] == '>') {
                        s->next++;
                        s->type = TOK_INFIX; s->expr.fun2 = shift_right;
                    }
                    else {
                        s->type = TOK_INFIX; s->expr.fun2 = greater;
                    }
                    break;
                case '&':
                    if (s->next++[0] == '&') {
                        s->type = TOK_INFIX; s->expr.fun2 = logical_and;
                    }
                    else {
                        s->next--;
                        s->type = TOK_INFIX; s->expr.fun2 = bitwise_and;
                    }
                    break;
                case '|':
                    if (s->next++[0] == '|') {
                        s->type = TOK_INFIX; s->expr.fun2 = logical_or;
                    }
                    else {
                        s->next--;
                        s->type = TOK_INFIX; s->expr.fun2 = bitwise_or;
                    }
                    break;
                case '^': s->type = TOK_INFIX; s->expr.fun2 = bitwise_xor; break;
                case '~': s->type = TOK_INFIX; s->expr.fun1 = bitwise_not; break;
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


static te_expr* list(state* s);  // comma separated expressions
static te_expr* expr(state* s);
static te_expr* power(state* s);

static te_expr* base(state* s) {
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr* ret;
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

		// Function without input parameters
    case TE_FUNCTION0:
    case TE_CLOSURE0:
        ret = new_expr(s->type, 0);
        CHECK_NULL(ret);

        ret->expr.fun0 = s->expr.fun0;

		// The last parameter of a closure is always a pointer to the context.
        if (IS_CLOSURE(s->type)) 
			ret->parameters[0] = s->context;

		// Expect an opening and then a closing parenthesis
        next_token(s);
        if (s->type == TOK_OPEN) {
            next_token(s);
            if (s->type != TOK_CLOSE) {
                s->type = TOK_ERROR;
            }
            else {
                next_token(s);
            }
        }
        break;

		// Function with 1 input parameter
    case TE_FUNCTION1:
    case TE_CLOSURE1:
        ret = new_expr(s->type, 0);
        CHECK_NULL(ret);

        ret->expr.fun1 = s->expr.fun1;

        if (IS_CLOSURE(s->type)) 
			ret->parameters[1] = s->context;

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

        if (IS_CLOSURE(s->type)) 
			ret->parameters[arity] = s->context;
			
        next_token(s);

		// Expect parenthesis to be opened
        if (s->type != TOK_OPEN) {
            s->type = TOK_ERROR;
        }
        else {
            int i;
            for (i = 0; i < arity; i++) {
                next_token(s);
                ret->parameters[i] = expr(s);
                CHECK_NULL(ret->parameters[i], te_free(ret));

                if (s->type != TOK_SEP) {
                    break;
                }
            }
            if (s->type != TOK_CLOSE || i != arity - 1) {
                s->type = TOK_ERROR;
            }
            else {
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
        }
        else {
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


static te_expr* power(state* s) {
    /* <power>     =    {("-" | "+" | "!")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->expr.fun2 == add || s->expr.fun2 == sub)) {
        if (s->expr.fun2 == sub) sign = -sign;
        next_token(s);
    }

    int logical = 0;
    while (s->type == TOK_INFIX && (s->expr.fun2 == add || s->expr.fun2 == sub || s->expr.fun1 == logical_not)) {
        if (s->expr.fun1 == logical_not) {
            if (logical == 0) {
                logical = -1;
            }
            else {
                logical = -logical;
            }
        }
        next_token(s);
    }

    te_expr* ret;

    if (sign == 1) {
        if (logical == 0) {
            ret = base(s);
        }
        else if (logical == -1) {
            ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
            ret->expr.fun1 = logical_not;
        }
        else {
            ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, base(s));
            ret->expr.fun1 = logical_notnot;
        }
    }
    else {
        te_expr* b = base(s);
        CHECK_NULL(b);

        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, b);
        CHECK_NULL(ret, te_free(b));

        if (logical == 0) {
            ret->expr.fun1 = negate;
        }
        else if (logical == -1) {
            ret->expr.fun1 = negate_logical_not;
        }
        else {
            ret->expr.fun1 = negate_logical_notnot;
        }
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT

static te_expr* factor(state* s) {
    /* <factor>    =    <power> {"**" <power>} */
    te_expr* ret = power(s);
    CHECK_NULL(ret);

    te_fun1 left_function = NULL;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) &&
        (ret->expr.fun1 == negate || ret->expr.fun1 == logical_not || ret->expr.fun1 == logical_notnot ||
            ret->expr.fun1 == negate_logical_not || ret->expr.fun1 == negate_logical_notnot)) {
        left_function = ret->expr.fun1;
        te_expr* se = ret->parameters[0];
        free(ret);
        ret = se;
    }

    te_expr* insertion = 0;
    te_fun2 dpow = pow; /* resolve overloading for g++ */
    while (s->type == TOK_INFIX && (s->expr.fun2 == dpow)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);

        if (insertion) {
            /* Make exponentiation go right-to-left. */
            te_expr* p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr* insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], p);
            CHECK_NULL(insert, te_free(p), te_free(ret));

            insert->expr.fun2 = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        }
        else {
            te_expr* p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr* prev = ret;
            ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
            CHECK_NULL(ret, te_free(p), te_free(prev));

            ret->expr.fun2 = t;
            insertion = ret;
        }
    }

    if (left_function) {
        te_expr* prev = ret;
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        ret->expr.fun1 = left_function;
        CHECK_NULL(ret, te_free(prev));
    }

    return ret;
}

#else

static te_expr* factor(state* s) {
    /* <factor>    =    <power> {"**" <power>} */
    te_expr* ret = power(s);
    CHECK_NULL(ret);

    te_fun1 left_function = NULL;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) &&
        (ret->expr.fun1 == negate || ret->expr.fun1 == logical_not || ret->expr.fun1 == logical_notnot ||
            ret->expr.fun1 == negate_logical_not || ret->expr.fun1 == negate_logical_notnot)) {
        left_function = ret->expr.fun1;
        te_expr* se = ret->parameters[0];
        free(ret);
        ret = se;
    }

    te_expr* insertion = 0;
    te_fun2 dpow = pow; /* resolve overloading for g++ */
    while (s->type == TOK_INFIX && (s->expr.fun2 == dpow)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);

        if (insertion) {
            /* TODO: Make exponentiation go left-ro-right. */
            te_expr* p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr* insert = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, insertion->parameters[1], p);
            CHECK_NULL(insert, te_free(p), te_free(ret));

            insert->expr.fun2 = t;
            insertion->parameters[1] = insert;
            insertion = insert;
        }
        else {
            te_expr* p = power(s);
            CHECK_NULL(p, te_free(ret));

            te_expr* prev = ret;
            ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, p);
            CHECK_NULL(ret, te_free(p), te_free(prev));

            ret->expr.fun2 = t;
            insertion = ret;
        }
    }

    if (left_function) {
        te_expr* prev = ret;
        ret = NEW_EXPR(TE_FUNCTION1 | TE_FLAG_PURE, ret);
        ret->expr.fun1 = left_function;
        CHECK_NULL(ret, te_free(prev));
    }

    return ret;
}

#endif

static te_expr* term(state* s) {
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr* ret = factor(s);
    CHECK_NULL(ret);
    te_fun2 dmod = fmod; /* resolve c++ overloading */
    while (s->type == TOK_INFIX && (s->expr.fun2 == mul || s->expr.fun2 == divide || s->expr.fun2 == dmod)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        te_expr* f = factor(s);
        CHECK_NULL(f, te_free(ret));

        te_expr* prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, f);
        CHECK_NULL(ret, te_free(f), te_free(prev));

        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* sum_expr(state* s) {
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr* ret = term(s);
    CHECK_NULL(ret);

    while (s->type == TOK_INFIX && (s->expr.fun2 == add || s->expr.fun2 == sub)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        te_expr* te = term(s);
        CHECK_NULL(te, te_free(ret));

        te_expr* prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, te);
        CHECK_NULL(ret, te_free(te), te_free(prev));

        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* shift_expr(state* s) {
    /* <expr>      =    <sum_expr> {("<<" | ">>") <sum_expr>} */
    te_expr* ret = sum_expr(s);

    while (s->type == TOK_INFIX && (s->expr.fun2 == shift_left || s->expr.fun2 == shift_right)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, sum_expr(s));
        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* test_expr(state* s) {
    /* <expr>      =    <shift_expr> {(">" | ">=" | "<" | "<=" | "==" | "!=") <shift_expr>} */
    te_expr* ret = shift_expr(s);

    while (s->type == TOK_INFIX && (s->expr.fun2 == greater || s->expr.fun2 == greater_eq ||
        s->expr.fun2 == lower || s->expr.fun2 == lower_eq || s->expr.fun2 == equal || s->expr.fun2 == not_equal)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, shift_expr(s));
        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* bitw_expr(state* s) {
    /* <expr>      =    <test_expr> {("&" | "|" | "^") <test_expr>} */
    te_expr* ret = test_expr(s);

    while (s->type == TOK_INFIX && (s->expr.fun2 == bitwise_and || s->expr.fun2 == bitwise_or || s->expr.fun2 == bitwise_xor)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, test_expr(s));
        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* expr(state* s) {
    /* <expr>      =    <bitw_expr> {("&&" | "||") <bitw_expr>} */
    te_expr* ret = bitw_expr(s);

    while (s->type == TOK_INFIX && (s->expr.fun2 == logical_and || s->expr.fun2 == logical_or)) {
        te_fun2 t = s->expr.fun2;
        next_token(s);
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, bitw_expr(s));
        ret->expr.fun2 = t;
    }

    return ret;
}


static te_expr* list(state* s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr* ret = expr(s);
    CHECK_NULL(ret);

    while (s->type == TOK_SEP) {
        next_token(s);
        te_expr* e = expr(s);
        CHECK_NULL(e, te_free(ret));

        te_expr* prev = ret;
        ret = NEW_EXPR(TE_FUNCTION2 | TE_FLAG_PURE, ret, e);
        CHECK_NULL(ret, te_free(e), te_free(prev));

        ret->expr.fun2 = comma;
    }

    return ret;
}


#define M(e) te_eval(n->parameters[e])


double te_eval(const te_expr* n) {
    if (!n) return NAN;

    switch (TYPE_MASK(n->type)) {
    case TE_CONSTANT: return n->expr.value;
    case TE_VARIABLE: return *n->expr.bound;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        switch (ARITY(n->type)) {
        case 0: return n->expr.fun0();
        case 1: return n->expr.fun1(M(0));
        case 2: return n->expr.fun2(M(0), M(1));
        case 3: return n->expr.fun3(M(0), M(1), M(2));
        case 4: return n->expr.fun4(M(0), M(1), M(2), M(3));
        case 5: return n->expr.fun5(M(0), M(1), M(2), M(3), M(4));
        case 6: return n->expr.fun6(M(0), M(1), M(2), M(3), M(4), M(5));
        case 7: return n->expr.fun7(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
        default: return NAN;
        }

    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
        switch (ARITY(n->type)) {
        case 0: return n->expr.clo0(n->parameters[0]);
        case 1: return n->expr.clo1(n->parameters[1], M(0));
        case 2: return n->expr.clo2(n->parameters[2], M(0), M(1));
        case 3: return n->expr.clo3(n->parameters[3], M(0), M(1), M(2));
        case 4: return n->expr.clo4(n->parameters[4], M(0), M(1), M(2), M(3));
        case 5: return n->expr.clo5(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
        case 6: return n->expr.clo6(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
        case 7: return n->expr.clo7(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
        default: return NAN;
        }

    default: return NAN;
    }

}

#undef M

static void optimize(te_expr* n) {
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
            if (n->parameters[i]->type != TE_CONSTANT) {
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


te_expr* te_compile(const char* expression, const te_variable* variables, int var_count, int* error) {
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr* root = list(&s);
    if (root == NULL) {
        if (error)
            *error = -1;
        return NULL;
    }

    if (s.type != TOK_END) {
        if (error) {
            *error = (s.next - s.start);
            if (*error == 0)
                *error = 1;
        }
        te_free(root);
        return 0;
    }
    else {
        optimize(root);
        if (error)
            *error = 0;
        return root;
    }
}


double te_interp(const char* expression, int* error) {
    te_expr* n = te_compile(expression, 0, 0, error);
    if (n == NULL) {
        return NAN;
    }

    double ret;
    if (n) {
        ret = te_eval(n);
        te_free(n);
    }
    else {
        ret = NAN;
    }
    return ret;
}

static void pn(const te_expr* n, int depth) {
    int i, arity;
    printf("%*s", depth, "");

    switch (TYPE_MASK(n->type)) {
    case TE_CONSTANT: printf("%f\n", n->expr.value); break;
    case TE_VARIABLE: printf("bound %p\n", n->expr.bound); break;

    case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
    case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
    case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
    case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
        arity = ARITY(n->type);
        printf("f%d", arity);
        for (i = 0; i < arity; i++) {
            printf(" %p", n->parameters[i]);
        }
        printf("\n");
        for (i = 0; i < arity; i++) {
            pn(n->parameters[i], depth + 1);
        }
        break;
    }
}


void te_print(const te_expr* n) {
    pn(n, 0);
}
