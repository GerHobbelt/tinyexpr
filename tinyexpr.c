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
#include <limits.h>

#ifndef NAN
#define NAN (0.0/0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0/0.0)
#endif


typedef double (*te_fun2)(double, double);




typedef struct state 
{
    const char *start;
    const char *next;
    te_type type;
    union {double value; const double *bound; const void *function;};
    void *context;

    const te_variable *lookup;
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


// Why use this generalized version, if we could use different ones for the
// different types, since we always know the type? Like e.g. for expressions that are numbers
// I feel like this would make a bit more sense.
static te_expr *new_expr_function(const te_type type, const void* function, const te_expr *parameters[]) 
{
    const int arity = ARITY(type);
    const int param_size = sizeof(void*) * arity;

    // Why is there a - sizeof(void*)? https://www.wikiwand.com/en/Flexible_array_member 
    // says you can declare flexible arrays simply with bracket[] syntax. No [1] necessary.
    // Or is this a question about compiler compatibility?
    const int size = (sizeof(te_expr) - sizeof(void*)) + param_size + (IS_CLOSURE(type) ? sizeof(void*) : 0);

    // Is calloc() not available for older compilers? It does these two, but a bit faster.
    te_expr *ret = malloc(size);
    memset(ret, 0, size);

    // Why clear the memory, if you're setting it all back up?
    if (arity && parameters) 
    {
        memcpy(ret->parameters, parameters, param_size);
    }
    ret->type = type;
    ret->bound = 0;

    ret->function = function;

    return ret;
}

static te_expr *new_expr_not_function(const te_type type)
{
    const int size = sizeof(te_expr) - sizeof(void*);
    te_expr *ret = calloc(1, size);
    ret->type = type;
    return ret;
}

static te_expr *new_expr_constant(double value)
{
    te_expr* result = new_expr_not_function(TE_CONSTANT);
    result->value = value;
    return result;
}

static te_expr *new_expr_variable(const double* bound)
{
    te_expr* result = new_expr_not_function(TE_VARIABLE);
    result->bound = bound;
    return result;
}

#define new_expr_function_with_params(type, fun, ...) new_expr_function((type), (fun), (const te_expr*[]){__VA_ARGS__})


void te_free_parameters(te_expr *n) 
{
    if (!n) return;

    // Is switch really faster than a for loop? We need to time it, really.
    // To me, this is cleaner than fall through's.
    int arity = ARITY(n->type);
    for (int i = 0; i < arity; i++)
    {
        te_free(n->parameters[i]);
    }

    // switch (TYPE_MASK(n->type)) 
    // {
    //     case TE_FUNCTION7: case TE_CLOSURE7: te_free(n->parameters[6]);     /* Falls through. */
    //     case TE_FUNCTION6: case TE_CLOSURE6: te_free(n->parameters[5]);     /* Falls through. */
    //     case TE_FUNCTION5: case TE_CLOSURE5: te_free(n->parameters[4]);     /* Falls through. */
    //     case TE_FUNCTION4: case TE_CLOSURE4: te_free(n->parameters[3]);     /* Falls through. */
    //     case TE_FUNCTION3: case TE_CLOSURE3: te_free(n->parameters[2]);     /* Falls through. */
    //     case TE_FUNCTION2: case TE_CLOSURE2: te_free(n->parameters[1]);     /* Falls through. */
    //     case TE_FUNCTION1: case TE_CLOSURE1: te_free(n->parameters[0]);
    // }
}


void te_free(te_expr *n) 
{
    if (!n) return;
    te_free_parameters(n);
    free(n);
}


static double pi(void) { return 3.14159265358979323846; }
static double e(void)  { return 2.71828182845904523536; }

static double factorial(double a) /* simplest version of factorial */
{
    if (a < 0.0) return NAN;
    if (a > UINT_MAX) return INFINITY;

    unsigned int ua = (unsigned int)(a);
    unsigned long int result = 1, i;

    for (i = 1; i <= ua; i++) 
    {
        if (i > ULONG_MAX / result)
            return INFINITY;
        result *= i;
    }
    return (double)result;
}

static double n_choose_r(double n, double r) 
{
    if (n < 0.0 || r < 0.0 || n < r) return NAN;
    if (n > UINT_MAX || r > UINT_MAX) return INFINITY;

    unsigned long int un = (unsigned int)(n);
    unsigned long int ur = (unsigned int)(r);
    if (ur > un / 2) ur = un - ur;

    unsigned long int result = 1;

    for (unsigned long int i = 1; i <= ur; i++) 
    {
        if (result > ULONG_MAX / (un - ur + i))
            return INFINITY;
        result *= un - ur + i;
        result /= i;
    }
    return result;
}

static double n_permute_r(double n, double r) { return n_choose_r(n, r) * factorial(r); }

static const te_variable functions[] = {
    /* must be in alphabetical order */
    {"abs", fabs,       TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"acos", acos,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"asin", asin,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan", atan,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"atan2", atan2,    TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"ceil", ceil,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cos", cos,        TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"cosh", cosh,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"e", e,            TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"exp", exp,        TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"fac", factorial,  TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"floor", floor,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ln", log,         TE_FUNCTION1 | TE_FLAG_PURE, 0},
#ifdef TE_NAT_LOG
    {"log", log,        TE_FUNCTION1 | TE_FLAG_PURE, 0},
#else
    {"log", log10,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
#endif
    {"log10", log10,    TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"ncr", n_choose_r, TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"npr", n_permute_r,TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"pi", pi,          TE_FUNCTION0 | TE_FLAG_PURE, 0},
    {"pow", pow,        TE_FUNCTION2 | TE_FLAG_PURE, 0},
    {"sin", sin,        TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sinh", sinh,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"sqrt", sqrt,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tan", tan,        TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {"tanh", tanh,      TE_FUNCTION1 | TE_FLAG_PURE, 0},
    {0, 0, 0, 0}
};


static const te_variable *find_builtin(const char *name, int len) 
{
    int imin = 0;
    int imax = sizeof(functions) / sizeof(te_variable) - 2;

    /* Binary search. */
    while (imax >= imin) 
    {
        const int i = imin + ((imax - imin) / 2);
        int comp_result = strncmp(name, functions[i].name, len);

        // In case the lengths are the same, verify if they both null terminate
        // Consider searching for sin. sinh would match too.
        if (!comp_result) comp_result = '\0' - functions[i].name[len];

        if (comp_result == 0) 
        {
            return functions + i;
        }
        else if (comp_result > 0) 
        {
            imin = i + 1;
        }
        else 
        {
            imax = i - 1;
        }
    }

    return NULL;
}

static const te_variable *find_lookup(const state *s, const char *name, int len) 
{
    int iters;
    const te_variable *var;
    if (!s->lookup) return 0;

    // Does a linear search. Could keep a sorted tree for these, maybe.
    // It doesn't really matter, since few variables will be defined, probably.
    for (var = s->lookup, iters = s->lookup_len; iters; ++var, --iters) 
    {
        if (strncmp(name, var->name, len) == 0 && var->name[len] == '\0') 
        {
            return var;
        }
    }
    return NULL;
}



static double add(double a, double b) { return a + b; }
static double sub(double a, double b) { return a - b; }
static double mul(double a, double b) { return a * b; }
static double divide(double a, double b) { return a / b; }
static double negate(double a) { return -a; }
static double comma(double a, double b) { (void)a; return b; }

static inline int is_alpha(char ch) 
{
    return ch >= 'a' && ch <= 'z';
}

static inline int is_numeric(char ch)
{
    return ch >= '0' && ch <= '9';
}

void next_token(state *s) 
{
    s->type = TOK_NULL;

    do {
        if (!*s->next){
            s->type = TOK_END;
            return;
        }

        /* Try reading a number. */
        if (is_numeric(s->next[0]) || s->next[0] == '.') 
        {
            s->value = strtod(s->next, (char**)&s->next);
            s->type = TOK_NUMBER;
        } 
        else 
        {
            /* Look for a variable or builtin function call. */
            if (is_alpha(s->next[0])) 
            {
                const char *start;
                start = s->next;
                while (is_alpha(s->next[0]) || is_numeric(s->next[0]) || (s->next[0] == '_')) s->next++;

                const te_variable *var = find_lookup(s, start, s->next - start);
                if (!var) var = find_builtin(start, s->next - start);

                if (!var)
                {
                    // TODO: Maybe add better errors?
                    s->type = TOK_ERROR;
                } 
                else 
                {
                    switch(TYPE_MASK(var->type))
                    {
                        case TE_VARIABLE:
                            s->type = TOK_VARIABLE;
                            s->bound = var->address;
                            break;

                        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:         /* Falls through. */
                        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:         /* Falls through. */
                            s->context = var->context;                                                  /* Falls through. */

                        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:     /* Falls through. */
                        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:     /* Falls through. */
                            s->type = var->type;
                            s->function = var->address;
                            break;
                    }
                }

            } 
            else 
            {
                /* Look for an operator or special character. */
                switch (s->next[0]) 
                {
                    case '+': s->type = TOK_INFIX; s->function = add; break;
                    case '-': s->type = TOK_INFIX; s->function = sub; break;
                    case '*': s->type = TOK_INFIX; s->function = mul; break;
                    case '/': s->type = TOK_INFIX; s->function = divide; break;
                    case '^': s->type = TOK_INFIX; s->function = pow; break;
                    case '%': s->type = TOK_INFIX; s->function = fmod; break;
                    case '(': s->type = TOK_OPEN;  break;
                    case ')': s->type = TOK_CLOSE; break;
                    case ',': s->type = TOK_SEP; break;
                    case ' ': case '\t': case '\n': case '\r': break;
                    default: s->type = TOK_ERROR; break;
                }
                s->next++;
            }
        }
    } 
    while (s->type == TOK_NULL);
}


static te_expr *list(state *s);  // comma separated expressions
static te_expr *expr(state *s);  
static te_expr *power(state *s);

static te_expr *base(state *s) 
{
    /* <base>      =    <constant> | <variable> | <function-0> {"(" ")"} | <function-1> <power> | <function-X> "(" <expr> {"," <expr>} ")" | "(" <list> ")" */
    te_expr *result;

    switch (TYPE_MASK(s->type)) {
        case TOK_NUMBER:
            result = new_expr_not_function(TE_CONSTANT); // a constant has no input parameters
            result->value = s->value;
            next_token(s);
            break;

        case TOK_VARIABLE:
            result = new_expr_not_function(TE_VARIABLE); // a variable has no input parameters
            result->bound = s->bound;
            next_token(s);
            break;

        // Function without input parameters
        case TE_FUNCTION0:
        case TE_CLOSURE0:
            result = new_expr_function(s->type, s->function, NULL); // no input parameters

            // The last parameter of a closure is always a pointer to the context.
            if (IS_CLOSURE(s->type)) 
            {
                result->parameters[0] = s->context;
            }

            // Expect an opening and then a closing parenthesis
            next_token(s);
            if (s->type == TOK_OPEN) 
            {
                next_token(s);
                if (s->type != TOK_CLOSE) 
                {
                    s->type = TOK_ERROR;
                } 
                else 
                {
                    next_token(s);
                }
            }
            break;

        // Function with 1 input parameter
        case TE_FUNCTION1:
        case TE_CLOSURE1:
            result = new_expr_function(s->type, s->function, NULL);

            if (IS_CLOSURE(s->type)) 
            {
                result->parameters[1] = s->context;
            }

            next_token(s);
            // ?? doesn't this raise the parameter to that power?
            result->parameters[0] = power(s);
            break;

        case TE_FUNCTION2: case TE_FUNCTION3: case TE_FUNCTION4:
        case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE2: case TE_CLOSURE3: case TE_CLOSURE4:
        case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
        {    
            int arity = ARITY(s->type);

            result = new_expr_function(s->type, s->function, NULL);
            if (IS_CLOSURE(s->type)) 
            {
                result->parameters[arity] = s->context;
            }
            next_token(s);

            // Expect parenthesis to be opened
            if (s->type != TOK_OPEN) 
            {
                s->type = TOK_ERROR;
            } 
            else
            {
                int i;
                for (i = 0; i < arity; i++) 
                {
                    next_token(s);
                    result->parameters[i] = expr(s);
                    if (s->type != TOK_SEP) 
                    {
                        break;
                    }
                }
                if (s->type != TOK_CLOSE || i != arity - 1) 
                {
                    s->type = TOK_ERROR;
                }
                else
                {
                    next_token(s);
                }
            }

            break;
        }

        case TOK_OPEN:
            next_token(s);
            result = list(s);
            if (s->type != TOK_CLOSE) 
            {
                s->type = TOK_ERROR;
            } 
            else 
            {
                next_token(s);
            }
            break;

        default:
            result = new_expr_not_function(0);
            s->type = TOK_ERROR;
            result->value = NAN;
            break;
    }

    return result;
}


static te_expr *power(state *s) {
    /* <power>     =    {("-" | "+")} <base> */
    int sign = 1;
    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) 
    {
        if (s->function == sub) sign = -sign;
        next_token(s);
    }

    te_expr *ret;

    if (sign == 1) 
    {
        ret = base(s);
    } 
    else 
    {
        ret = new_expr_function_with_params(TE_FUNCTION1 | TE_FLAG_PURE, negate, base(s));
    }

    return ret;
}

#ifdef TE_POW_FROM_RIGHT
static te_expr *factor(state *s) 
{
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    int neg = 0;

    if (ret->type == (TE_FUNCTION1 | TE_FLAG_PURE) && ret->function == negate) 
    {
        te_expr *se = ret->parameters[0];
        free(ret);
        ret = se;
        neg = 1;
    }

    te_expr *insertion = 0;

    while (s->type == TOK_INFIX && (s->function == pow)) 
    {
        te_fun2 t = s->function;
        next_token(s);

        if (insertion) 
        {
            /* Make exponentiation go right-to-left. */
            te_expr *insert = new_expr_function_with_params(
                TE_FUNCTION2 | TE_FLAG_PURE, t, insertion->parameters[1], power(s)
            );
            insertion->parameters[1] = insert;
            insertion = insert;
        } 
        else 
        {
            ret = new_expr_function_with_params(TE_FUNCTION2 | TE_FLAG_PURE, t, ret, power(s));
            insertion = ret;
        }
    }

    if (neg) 
    {
        ret = new_expr_function_with_params(TE_FUNCTION1 | TE_FLAG_PURE, negate, ret);
    }

    return ret;
}
#else
static te_expr *factor(state *s) 
{
    /* <factor>    =    <power> {"^" <power>} */
    te_expr *ret = power(s);

    while (s->type == TOK_INFIX && (s->function == pow)) 
    {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr_function_with_params(TE_FUNCTION2 | TE_FLAG_PURE, t, ret, power(s));
    }

    return ret;
}
#endif



static te_expr *term(state *s) 
{
    /* <term>      =    <factor> {("*" | "/" | "%") <factor>} */
    te_expr *ret = factor(s);

    while (s->type == TOK_INFIX && (s->function == mul || s->function == divide || s->function == fmod)) 
    {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr_function_with_params(TE_FUNCTION2 | TE_FLAG_PURE, t, ret, factor(s));
    }

    return ret;
}


static te_expr *expr(state *s) 
{
    /* <expr>      =    <term> {("+" | "-") <term>} */
    te_expr *ret = term(s);

    while (s->type == TOK_INFIX && (s->function == add || s->function == sub)) 
    {
        te_fun2 t = s->function;
        next_token(s);
        ret = new_expr_function_with_params(TE_FUNCTION2 | TE_FLAG_PURE, t, ret, term(s));
    }

    return ret;
}


static te_expr *list(state *s) {
    /* <list>      =    <expr> {"," <expr>} */
    te_expr *ret = expr(s);

    while (s->type == TOK_SEP) 
    {
        next_token(s);
        ret = new_expr_function_with_params(TE_FUNCTION2 | TE_FLAG_PURE, comma, ret, expr(s));
    }

    return ret;
}


#define TE_FUN(...) ((double(*)(__VA_ARGS__))n->function)
#define M(e) te_eval(n->parameters[e])


double te_eval(const te_expr *n) {
    if (!n) return NAN;

    switch(TYPE_MASK(n->type)) {
        case TE_CONSTANT: return n->value;
        case TE_VARIABLE: return *n->bound;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void)();
                case 1: return TE_FUN(double)(M(0));
                case 2: return TE_FUN(double, double)(M(0), M(1));
                case 3: return TE_FUN(double, double, double)(M(0), M(1), M(2));
                case 4: return TE_FUN(double, double, double, double)(M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(double, double, double, double, double, double, double)(M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
            switch(ARITY(n->type)) {
                case 0: return TE_FUN(void*)(n->parameters[0]);
                case 1: return TE_FUN(void*, double)(n->parameters[1], M(0));
                case 2: return TE_FUN(void*, double, double)(n->parameters[2], M(0), M(1));
                case 3: return TE_FUN(void*, double, double, double)(n->parameters[3], M(0), M(1), M(2));
                case 4: return TE_FUN(void*, double, double, double, double)(n->parameters[4], M(0), M(1), M(2), M(3));
                case 5: return TE_FUN(void*, double, double, double, double, double)(n->parameters[5], M(0), M(1), M(2), M(3), M(4));
                case 6: return TE_FUN(void*, double, double, double, double, double, double)(n->parameters[6], M(0), M(1), M(2), M(3), M(4), M(5));
                case 7: return TE_FUN(void*, double, double, double, double, double, double, double)(n->parameters[7], M(0), M(1), M(2), M(3), M(4), M(5), M(6));
                default: return NAN;
            }

        default: return NAN;
    }

}

#undef TE_FUN
#undef M

static void optimize(te_expr *n) 
{
    /* Evaluates as much as possible. */
    if (n->type == TE_CONSTANT) return;
    if (n->type == TE_VARIABLE) return;

    /* Only optimize out functions flagged as pure. */
    if (IS_PURE(n->type)) 
    {
        const int arity = ARITY(n->type);
        int known = 1;
        int i;
        for (i = 0; i < arity; ++i) 
        {
            optimize(n->parameters[i]);
            if (((te_expr*)(n->parameters[i]))->type != TE_CONSTANT) 
            {
                known = 0;
            }
        }
        if (known) 
        {
            const double value = te_eval(n);
            te_free_parameters(n);
            n->type = TE_CONSTANT;
            n->value = value;
        }
    }
}


te_expr *te_compile(const char *expression, const te_variable *variables, int var_count, int *error) 
{
    state s;
    s.start = s.next = expression;
    s.lookup = variables;
    s.lookup_len = var_count;

    next_token(&s);
    te_expr *root = list(&s);

    if (s.type != TOK_END) 
    {
        te_free(root);
        if (error) 
        {
            *error = (s.next - s.start);
            if (*error == 0) *error = 1;
        }
        return 0;
    } 
    else 
    {
        optimize(root);
        if (error) *error = 0;
        return root;
    }
}


double te_interp(const char *expression, int *error) 
{
    te_expr *n = te_compile(expression, 0, 0, error);
    double ret;
    if (n) 
    {
        ret = te_eval(n);
        te_free(n);
    } 
    else 
    {
        ret = NAN;
    }
    return ret;
}

static void pn (const te_expr *n, int depth) 
{
    printf("%*s", depth, "");

    switch(TYPE_MASK(n->type)) 
    {
        case TE_CONSTANT: 
            printf("%f\n", n->value); 
            break;

        case TE_VARIABLE: 
            printf("bound %p\n", n->bound); 
            break;

        case TE_FUNCTION0: case TE_FUNCTION1: case TE_FUNCTION2: case TE_FUNCTION3:
        case TE_FUNCTION4: case TE_FUNCTION5: case TE_FUNCTION6: case TE_FUNCTION7:
        case TE_CLOSURE0: case TE_CLOSURE1: case TE_CLOSURE2: case TE_CLOSURE3:
        case TE_CLOSURE4: case TE_CLOSURE5: case TE_CLOSURE6: case TE_CLOSURE7:
        {
            int arity = ARITY(n->type);
            printf("f%d", arity);
            for (int i = 0; i < arity; i++) 
            {
                printf(" %p", n->parameters[i]);
            }
            printf("\n");
            for (int i = 0; i < arity; i++) 
            {
                pn(n->parameters[i], depth + 1);
            }
            break;
        }
    }
}


void te_print(const te_expr *n) 
{
    pn(n, 0);
}

te_expr* te_expr_deep_copy(const te_expr* expr)
{
    te_expr* result = NULL;

    switch(TYPE_MASK(expr->type)) 
    {
        case TE_CONSTANT: {
            result = new_expr_constant(expr->value);
            break;
        }

        case TE_VARIABLE: {
            result = new_expr_variable(expr->bound);
            break;
        }

        case TE_CLOSURE0: case TE_FUNCTION0:
        case TE_CLOSURE1: case TE_FUNCTION1:
        case TE_CLOSURE2: case TE_FUNCTION2:
        case TE_CLOSURE3: case TE_FUNCTION3:
        case TE_CLOSURE4: case TE_FUNCTION4:
        case TE_CLOSURE5: case TE_FUNCTION5:
        case TE_CLOSURE6: case TE_FUNCTION6:
        case TE_CLOSURE7: case TE_FUNCTION7: 
        {
            result = new_expr_function(expr->type, expr->function, NULL);

            int arity = ARITY(expr->type);
            for (int i = 0; i < arity; i++)
            {
                result->parameters[i] = te_expr_deep_copy(expr->parameters[i]);
            }

            if (IS_CLOSURE(expr->type))
            {
                result->parameters[arity] = expr->parameters[arity];
            }

            break;
        }
    }

    return result;
}


static te_expr* differentiate_symbolically(const te_expr* expr, const void* variable);

// (f(x))' = x' * f'(x)
// Does not take closures into account
static te_expr* power_rule_one_parameter_function(void* derivative_function, te_expr* inner_expr, const void* variable)
{
    te_expr* inner_derivative = differentiate_symbolically(inner_expr, variable);
    te_expr* rhs_expr = new_expr_function_with_params(
        TE_FUNCTION1|TE_FLAG_PURE, derivative_function, te_expr_deep_copy(inner_expr)
    );
    return new_expr_function_with_params(TE_FUNCTION2|TE_FLAG_PURE, mul, rhs_expr, inner_derivative);
}

static te_expr* differentiate_symbolically(const te_expr* expr, const void* variable)
{
    te_expr* result = NULL;

    switch(TYPE_MASK(expr->type)) 
    {
        // c' = 0
        case TE_CONSTANT: case TE_FUNCTION0: case TE_CLOSURE0: 
        {
            result = new_expr_constant(0);
            break;
        }
        
        // x' = 1
        case TE_VARIABLE: 
        {
            result = new_expr_constant(expr->bound == variable ? 1 : 0);
            break;
        }

        case TE_FUNCTION1: 
        {
            te_expr* a = expr->parameters[0];

            // for now, do an else if over the known types
            // (-a)' = -(a')
            if (expr->function == negate)
            {
                result = new_expr_function_with_params(
                    TE_FUNCTION1|TE_FLAG_PURE, negate, differentiate_symbolically(a, variable));
            }

            // sin(a)' = cos(a) * a'
            else if (expr->function == sin)
            {
                result = power_rule_one_parameter_function(cos, a, variable);
            }

            // cos(a)' = -sin(a) * a'
            else if (expr->function == cos)
            {
                const te_expr* sin_expr = power_rule_one_parameter_function(sin, a, variable);
                result = new_expr_function_with_params(TE_FUNCTION1|TE_FLAG_PURE, negate, sin_expr);
            }

            // ln(a)' = a' / a
            else if (expr->function == log)
            {
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, divide, 
                    differentiate_symbolically(a, variable),
                    te_expr_deep_copy(a)
                );
            }
            
            // (e^a)' = e^a * a'
            else if (expr->function == exp)
            {
                result = power_rule_one_parameter_function(exp, a, variable);
            }

            else
            {
                printf("Unsupported function.\n");
            }
            break;
        }

        case TE_FUNCTION2: 
        {
            const te_expr* a = expr->parameters[0];
            const te_expr* b = expr->parameters[1];
            te_expr* a_prime = differentiate_symbolically(a, variable);
            te_expr* b_prime = differentiate_symbolically(b, variable);

            // (a + b)' = a' + b'
            if (expr->function == add)
            {
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, add, a_prime, b_prime
                );
            }

            // (a - b)' = a' - b'
            else if (expr->function == sub)
            {
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, sub, a_prime, b_prime
                );
            }

            // (a * b)' = a' * b + a * b'
            else if (expr->function == mul)
            {
                te_expr* a_copy  = te_expr_deep_copy(a);
                te_expr* b_copy  = te_expr_deep_copy(b);
                te_expr* mul_1 = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, a_prime, b_copy
                );
                te_expr* mul_2 = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, b_prime, a_copy
                );
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, add, mul_1, mul_2
                );
            }

            // (a / b)' = (a' * b - a * b') / b^2
            else if (expr->function == divide)
            {
                te_expr* b_squared = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, pow, te_expr_deep_copy(b), new_expr_constant(2)
                );
                te_expr* mul_1 = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, a_prime, te_expr_deep_copy(b)
                );
                te_expr* mul_2 = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, b_prime, te_expr_deep_copy(a)
                );
                te_expr* subtraction = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, add, mul_1, mul_2
                );
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, divide, subtraction, b_squared
                );
            }

            // (a^b)' = a^b * (a' * b / a + b' * ln(a))
            else if (expr->function == pow)
            {
                te_expr* a_prime_times_b = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, a_prime, te_expr_deep_copy(b)
                );
                te_expr* a_prime_times_b_over_a = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, divide, a_prime_times_b, te_expr_deep_copy(a)
                );
                te_expr* ln_a = new_expr_function_with_params(
                    TE_FUNCTION1|TE_FLAG_PURE, log, te_expr_deep_copy(a)
                );
                te_expr* b_prime_times_ln_a = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, b_prime, ln_a
                );
                te_expr* sum = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, add, a_prime_times_b_over_a, b_prime_times_ln_a
                );
                result = new_expr_function_with_params(
                    TE_FUNCTION2|TE_FLAG_PURE, mul, te_expr_deep_copy(expr), sum
                );
            }

            else
            {
                printf("Unsupported function.\n");
            }
            break;
        }

        default:
            printf("Custom functions are not supported.\n");
    }
    return result;
}


te_expr* te_differentiate_symbolically(const te_expr* expression, const te_variable* variable, int* error)
{
    te_expr* result = differentiate_symbolically(expression, variable->address);
    optimize(result);
    return result;
}