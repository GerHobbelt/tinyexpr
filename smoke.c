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

#include "tinyexpr.h"
#include <stdio.h>
#include "minctest.h"

#include "monolithic_examples.h"

#ifndef M_E
#define M_E  (exp(1))
#endif

#ifndef M_PI
#define M_PI  (acos(-1))
#endif

#define nelem(a)   ( sizeof(a) / sizeof((a)[0]) )

typedef struct {
    const char* expr;
    double answer;
} test_case;

typedef struct {
    const char* expr1;
    const char* expr2;
} test_equ;


static int isNaN(double f)
{
	// the standard (f != f) check doesn't work for MSVC(2022) as the default NAN assigned is a NaN which apparently doesn't fail this standard check. Weird.
#if !defined(isnan)
	int rv = (f != f);
#else
	int rv = isnan(f);
#endif
	return rv;
}


static void test_results() {
    test_case cases[] = {
        {"1", 1},
        {"1 ", 1},
        {"(1)", 1},

        {"pi", M_PI},
        {"atan(1)*4 - pi", 0},
        {"e", M_E},

        {"2+1", 2 + 1},
        {"(((2+(1))))", 2 + 1},
        {"3+2", 3 + 2},

        {"3+2+4", 3 + 2 + 4},
        {"(3+2)+4", 3 + 2 + 4},
        {"3+(2+4)", 3 + 2 + 4},
        {"(3+2+4)", 3 + 2 + 4},

        {"3*2*4", 3 * 2 * 4},
        {"(3*2)*4", 3 * 2 * 4},
        {"3*(2*4)", 3 * 2 * 4},
        {"(3*2*4)", 3 * 2 * 4},

        {"3-2-4", 3 - 2 - 4},
        {"(3-2)-4", (3 - 2) - 4},
        {"3-(2-4)", 3 - (2 - 4)},
        {"(3-2-4)", 3 - 2 - 4},

        {"3/2/4", 3.0 / 2.0 / 4.0},
        {"(3/2)/4", (3.0 / 2.0) / 4.0},
        {"3/(2/4)", 3.0 / (2.0 / 4.0)},
        {"(3/2/4)", 3.0 / 2.0 / 4.0},

        {"(3*2/4)", 3.0 * 2.0 / 4.0},
        {"(3/2*4)", 3.0 / 2.0 * 4.0},
        {"3*(2/4)", 3.0 * (2.0 / 4.0)},

        {"asin sin .5", 0.5},
        {"sin asin .5", 0.5},
        {"ln exp .5", 0.5},
        {"exp ln .5", 0.5},

        {"asin sin-.5", -0.5},
        {"asin sin-0.5", -0.5},
        {"asin sin -0.5", -0.5},
        {"asin (sin -0.5)", -0.5},
        {"asin (sin (-0.5))", -0.5},
        {"asin sin (-0.5)", -0.5},
        {"(asin sin (-0.5))", -0.5},

        {"log10 1000", 3},
        {"log10 1e3", 3},
        {"log10 1000", 3},
        {"log10 1e3", 3},
        {"log10(1000)", 3},
        {"log10(1e3)", 3},
        {"log10 1.0e3", 3},
        {"10**5*5e-5", 5},

#ifdef TE_NAT_LOG
        {"log 1000", 6.9078},
        {"log e", 1},
        {"log (e**10)", 10},
#else
        {"log 1000", 3},
        {"log (10**e)", M_E},
        {"log (10**10)", 10},
#endif

        {"ln (e**10)", 10},
        {"100**.5+1", 11},
        {"100 **.5+1", 11},
        {"100**+.5+1", 11},
        {"100**--.5+1", 11},
        {"100**---+-++---++-+-+-.5+1", 11},

        {"100**-.5+1", 1.1},
        {"100**---.5+1", 1.1},
        {"100**+---.5+1", 1.1},
        {"1e2**+---.5e0+1e0", 1.1},
        {"--(1e2**(+(-(-(-.5e0))))+1e0)", 1.1},

        {"sqrt 100 + 7", 17},
        {"sqrt 100 * 7", 70},
        {"sqrt (100 * 100)", 100},

        {"1,2", 2},
        {"1,2+1", 3},
        {"1+1,2+2,2+1", 3},
        {"1,2,3", 3},
        {"(1,2),3", 3},
        {"1,(2,3)", 3},
        {"-(1,(2,3))", -3},

        {"2**2", 4},
		{"-2**2", +4},
		{"-(2**2)", -4},
		{"2**-2", pow(2, -2)},
		{"pow(2,2)", 4},

        {"atan2(1,1)", 0.7854},
        {"atan2(1,2)", 0.4636},
        {"atan2(2,1)", 1.1071},
        {"atan2(3,4)", 0.6435},
        {"atan2(3+3,4*2)", 0.6435},
        {"atan2(3+3,(4*2))", 0.6435},
        {"atan2((3+3),4*2)", 0.6435},
        {"atan2((3+3),(4*2))", 0.6435},

        { "1**1", 1 },
        { "1**5", 1 },

        { "!3", 0 },
        { "!+5", 0 },
        { "!-5", 0 },
        { "!0", 1 },
        { "!!!0", 1 },

        { "~3", 0x1FFFFFFFFFFFFFLL & ~3LL },
        { "~0", 0x1FFFFFFFFFFFFFLL & ~0LL },

		{ "1^^5", 0 },
		{ "1^5", 4 },
		{ "1&5", 1 },
		{ "1|5", 5 },
		{ "31&&5", 1 },
		{ "31||5", 1 },

    };


    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        const double ev = te_interp(expr, &err);
        lok(!err, expr);
        lfequal(ev, answer, expr);

        if (err) {
            printf("FAILED: [%s] --> (error position: %d)\n", expr, err);
        }
    }
}


void test_syntax() {
    test_case errors[] = {
        {"", 1},
        {"1+", 2},
        {"1)", 2},
        {"(1", 2},
        {"1***1", 4},
        {"1*2(+4", 4},
        {"1*2(1+4", 4},
        {"a+5", 1},
        {"_a+5", 2},
        {"#a+5", 1},
		{"A+5", 1},		// undefined variables...
		{"Aa+5", 2},
        {"1*^5", 3},
        {"1^*5", 3},
        {"sin(cos5", 8},
		{"cos5", 4},
	};


    int i;
    for (i = 0; i < nelem(errors); ++i) {
        const char* expr = errors[i].expr;
        const int e = errors[i].answer;

        int err;
        const double r = te_interp(expr, &err);
        lequal(err, e, expr);
        lok(isNaN(r), expr);

        te_expr* n = te_compile(expr, 0, 0, &err);
        lequal(err, e, expr);
        lok(!n, expr);

        if (err != e) {
            printf("FAILED: %s\n", expr);
        }

        const double k = te_interp(expr, 0);
        lok(isNaN(k), expr);
    }
}


static void test_unary_ops() {
	test_case cases[] = {
		{ "!~-1023", !(0x1FFFFFFFFFFFFFLL & ~- 1023LL) },
		{"+1", 1},
		{"-1 ", -1},
		{"!1", 0},

		{"-pi", -M_PI},
		{"-e", -M_E},

		{"100**---+-++---++-+-+-.5+1", 11},

		{ "!3", 0 },
		{ "!!+5", 1 },
		{ "!-5", 0 },
		{ "!0", 1 },
		{ "!!!0", 1 },

		{ "~3", 0x1FFFFFFFFFFFFFLL & ~3LL },
		{ "~0", 0x1FFFFFFFFFFFFFLL },
		{ "~-25", 0x1FFFFFFFFFFFFFLL & ~-25LL },
		{ "-~~~-1023", -(0x1FFFFFFFFFFFFFLL & ~~~-1023LL) },
		{ "~-1023", (0x1FFFFFFFFFFFFFLL & ~-1023LL) },
		{ "!~-1023", !(0x1FFFFFFFFFFFFFLL & ~- 1023LL) },
		{ "!!~-1023", !!(0x1FFFFFFFFFFFFFLL & ~-1023LL) },
		{ "~!!~-1023", (0x1FFFFFFFFFFFFFLL & ~!!(0x1FFFFFFFFFFFFFLL & ~-1023LL) ) },
		{ "~~!!~-1023", (0x1FFFFFFFFFFFFFLL & ~~!!(0x1FFFFFFFFFFFFFLL & ~-1023LL)) },
		{ "-~~!!~-1023", -(0x1FFFFFFFFFFFFFLL & ~~!!(0x1FFFFFFFFFFFFFLL & ~- 1023LL)) },
		{ "!!-1023", !!-1023 },
		{ "-!!--!!-1023", -!!-(-!!-1023)},
	};


	int i;
	for (i = 0; i < nelem(cases); ++i) {
		const char* expr = cases[i].expr;
		const double answer = cases[i].answer;

		int err;
		const double ev = te_interp(expr, &err);
		lok(!err, expr);
		lfequal(ev, answer, expr);

		if (err) {
			printf("FAILED: [%s] --> (error position: %d)\n", expr, err);
		}

		// all the test expressions should optimize to a TE_CONSTANT token by constant folding and unary operator folding.
		te_expr* n = te_compile(expr, 0, 0, &err);
		lok(n, expr);
		lequal(err, 0, expr);
		if (n)
		{
			lequal(n->type, TE_CONSTANT, expr);
			te_free(n);
		}
	}
}



void test_nans() {

    const char* nans[] = {
        "0/0",
        "1%0",
        "1%(1%0)",
        "(1%0)%1",
        "fac(-1)",
        "ncr(2, 4)",
        "ncr(-2, 4)",
        "ncr(2, -4)",
        "npr(2, 4)",
        "npr(-2, 4)",
        "npr(2, -4)",
    };

    int i;
    for (i = 0; i < nelem(nans); ++i) {
        const char* expr = nans[i];

        int err;
        const double r = te_interp(expr, &err);
        lequal(err, 0, expr);
        lok(isNaN(r), expr);

        te_expr* n = te_compile(expr, 0, 0, &err);
        lok(n, expr);
        lequal(err, 0, expr);
        const double c = te_eval(n);
        lok(isNaN(c), expr);
        te_free(n);
    }
}


void test_infs() {

    const char* infs[] = {
            "1/0",
            "log(0)",
            "pow(2,10000000)",
            "fac(300)",
            "ncr(300,100)",
            "ncr(300000,100)",
            "ncr(300000,100)*8",
            "npr(3,2)*ncr(300000,100)",
            "npr(100,90)",
            "npr(30,25)",
    };

    int i;
    for (i = 0; i < nelem(infs); ++i) {
        const char* expr = infs[i];

        int err;
        const double r = te_interp(expr, &err);
        lequal(err, 0, expr);
        lok(r == r + 1, expr);

        te_expr* n = te_compile(expr, 0, 0, &err);
        lok(n, expr);
        lequal(err, 0, expr);
        const double c = te_eval(n);
        lok(c == c + 1, expr);
        te_free(n);
    }
}


void test_variables() {

    double x, y, test;
    te_variable lookup[] = { {.name = "x", {.variable = &x }}, {.name = "y", {.variable = &y }}, {.name = "te_st", {.variable = &test }} };

    int err;

    te_expr* expr1 = te_compile("cos x + sin y", lookup, 2, &err);
    lok(expr1, "cos x + sin y");
    lok(!err, "cos x + sin y");

    te_expr* expr2 = te_compile("x+x+x-y", lookup, 2, &err);
    lok(expr2, "x+x+x-y");
    lok(!err, "x+x+x-y");

    te_expr* expr3 = te_compile("x*y**3", lookup, 2, &err);
    lok(expr3, "x*y**3");
    lok(!err, "x*y**3");

    te_expr* expr4 = te_compile("te_st+5", lookup, 3, &err);
    lok(expr4, "te_st+5");
    lok(!err, "te_st+5");

    for (y = 2; y < 3; ++y) {
        for (x = 0; x < 5; ++x) {
            double ev;

            ev = te_eval(expr1);
            lfequal(ev, cos(x) + sin(y), "cos x + sin y");

            ev = te_eval(expr2);
            lfequal(ev, x + x + x - y, "x+x+x-y");

            ev = te_eval(expr3);
            lfequal(ev, x * y * y * y, "x*y**3");

            test = x;
            ev = te_eval(expr4);
            lfequal(ev, x + 5, "te_st+5");
        }
    }

    te_free(expr1);
    te_free(expr2);
    te_free(expr3);
    te_free(expr4);



    te_expr* expr5 = te_compile("xx*y**3", lookup, 2, &err);
    lok(!expr5, "xx*y**3");
    lok(err, "xx*y**3");

    te_expr* expr6 = te_compile("tes", lookup, 3, &err);
    lok(!expr6, "tes");
    lok(err, "tes");

    te_expr* expr7 = te_compile("sinn x", lookup, 2, &err);
    lok(!expr7, "sinn x");
    lok(err, "sinn x");

    te_expr* expr8 = te_compile("si x", lookup, 2, &err);
    lok(!expr8, "si x");
    lok(err, "si x");
}


void test_variables2() {

	double x = 1, y = 2, a = 3, _a_ = 4, ca = 5, aa = 6;
	te_variable lookup[] = {
		{.name = "x", {.variable = &x }},
		{.name = "y", {.variable = &y }},
		{.name = "a", {.variable = &a }},
		{.name = "_a_", {.variable = &_a_ }},
		{.name = "A", {.variable = &ca }},
		{.name = "Aa", {.variable = &aa }},
	};

	test_case exprs[] = {
		{"x+5", 6},
		{"y+5", 7},
		{"a+5", 8},
		{"_a_+5", 9},
		{"A+5", 10},
		{"Aa+5", 11},
		{"x+y+a+_a_+A+Aa", 21},
	};

	int i;
	for (i = 0; i < nelem(exprs); ++i) {
		const char* expr = exprs[i].expr;
		double answer = exprs[i].answer;

		int err;
		te_expr* n = te_compile(expr, lookup, nelem(lookup), &err);
		lok(n, expr);
		lequal(err, 0, expr);
		const double c = te_eval(n);
		lfequal(c, answer, expr);
		te_free(n);
	}
}


#define cross_check(a, b) do {\
    if ((b)!=(b)) break;\
    expr = te_compile((a), lookup, 2, &err);\
    lfequal(te_eval(expr), (b), (a));\
    lok(!err, (a));\
    te_free(expr);\
}while(0)

void test_functions() {

    double x, y;
    te_variable lookup[] = { {.name = "x", {.variable = &x }}, {.name = "y", {.variable = &y }} };

    int err;
    te_expr* expr;

    for (x = -5; x < 5; x += .2) {
        cross_check("abs x", fabs(x));
        cross_check("acos x", acos(x));
        cross_check("asin x", asin(x));
        cross_check("atan x", atan(x));
        cross_check("ceil x", ceil(x));
        cross_check("cos x", cos(x));
        cross_check("cosh x", cosh(x));
        cross_check("exp x", exp(x));
        cross_check("floor x", floor(x));
        cross_check("ln x", log(x));
        cross_check("log10 x", log10(x));
        cross_check("sin x", sin(x));
        cross_check("sinh x", sinh(x));
        cross_check("sqrt x", sqrt(x));
        cross_check("tan x", tan(x));
        cross_check("tanh x", tanh(x));

        for (y = -2; y < 2; y += .2) {
            if (fabs(x) < 0.01) break;
            cross_check("atan2(x,y)", atan2(x, y));
            cross_check("pow(x,y)", pow(x, y));
        }
    }
}


double sum0(void) {
    return 6;
}
double sum1(double a) {
    return a * 2;
}
double sum2(double a, double b) {
    return a + b;
}
double sum3(double a, double b, double c) {
    return a + b + c;
}
double sum4(double a, double b, double c, double d) {
    return a + b + c + d;
}
double sum5(double a, double b, double c, double d, double e) {
    return a + b + c + d + e;
}
double sum6(double a, double b, double c, double d, double e, double f) {
    return a + b + c + d + e + f;
}
double sum7(double a, double b, double c, double d, double e, double f, double g) {
    return a + b + c + d + e + f + g;
}


void test_dynamic() {

    double x, f;
    te_variable lookup[] = {
        {"x", {.variable = &x}},
        {"f", {.variable = &f}},
        {"sum0", {.fun0 = sum0 }, TE_FUNCTION0},
        {"sum1", {.fun1 = sum1 }, TE_FUNCTION1},
        {"sum2", {.fun2 = sum2 }, TE_FUNCTION2},
        {"sum3", {.fun3 = sum3 }, TE_FUNCTION3},
        {"sum4", {.fun4 = sum4 }, TE_FUNCTION4},
        {"sum5", {.fun5 = sum5 }, TE_FUNCTION5},
        {"sum6", {.fun6 = sum6 }, TE_FUNCTION6},
        {"sum7", {.fun7 = sum7 }, TE_FUNCTION7},
    };

    test_case cases[] = {
        {"x", 2},
        {"f+x", 7},
        {"x+x", 4},
        {"x+f", 7},
        {"f+f", 10},
        {"f+sum0", 11},
        {"sum0+sum0", 12},
        {"sum0()+sum0", 12},
        {"sum0+sum0()", 12},
        {"sum0()+(0)+sum0()", 12},
        {"sum1 sum0", 12},
        {"sum1(sum0)", 12},
        {"sum1 f", 10},
        {"sum1 x", 4},
        {"sum2 (sum0, x)", 8},
        {"sum3 (sum0, x, 2)", 10},
        {"sum2(2,3)", 5},
        {"sum3(2,3,4)", 9},
        {"sum4(2,3,4,5)", 14},
        {"sum5(2,3,4,5,6)", 20},
        {"sum6(2,3,4,5,6,7)", 27},
        {"sum7(2,3,4,5,6,7,8)", 35},
    };

    x = 2;
    f = 5;

    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        te_expr* ex = te_compile(expr, lookup, nelem(lookup), &err);
        lok(ex, expr);
        lfequal(te_eval(ex), answer, expr);
        te_free(ex);
    }
}


double clo0(te_expr* context) {
    if (context) return *(context->expr.bound) + 6;
    return 6;
}
double clo1(te_expr* context, double a) {
    if (context) return *(context->expr.bound) + a * 2;
    return a * 2;
}
double clo2(te_expr* context, double a, double b) {
    if (context) return *(context->expr.bound) + a + b;
    return a + b;
}

double cell(te_expr* context, double a) {
    const double* c = context->expr.bound;
    return c[(int)a];
}

void test_closure() {

    double extra;
    double c[] = { 5,6,7,8,9 };

    te_expr closures[] = {
    {.expr = {.bound = &extra }},
    {.expr = {.bound = &extra }},
    {.expr = {.bound = &extra }},
    {.expr = {.bound = c }},
    };

    te_variable lookup[] = {
        {"c0", {.clo0 = clo0}, TE_CLOSURE0, .context = &closures[0]},
        {"c1", {.clo1 = clo1}, TE_CLOSURE1, .context = &closures[1]},
        {"c2", {.clo2 = clo2}, TE_CLOSURE2, .context = &closures[2]},
        {"cell", {.clo1 = cell}, TE_CLOSURE1, .context = &closures[3]},
    };

    test_case cases[] = {
        {"c0", 6},
        {"c1 4", 8},
        {"c2 (10, 20)", 30},
    };

    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        te_expr* ex = te_compile(expr, lookup, nelem(lookup), &err);
        lok(ex, expr);

        extra = 0;
        lfequal(te_eval(ex), answer + extra, expr);

        extra = 10;
        lfequal(te_eval(ex), answer + extra, expr);

        te_free(ex);
    }


    test_case cases2[] = {
        {"cell 0", 5},
        {"cell 1", 6},
        {"cell 0 + cell 1", 11},
        {"cell 1 * cell 3 + cell 4", 57},
    };

    for (i = 0; i < nelem(cases2); ++i) {
        const char* expr = cases2[i].expr;
        const double answer = cases2[i].answer;

        int err;
        te_expr* ex = te_compile(expr, lookup, nelem(lookup), &err);
        lok(ex, expr);
        lfequal(te_eval(ex), answer, expr);
        te_free(ex);
    }
}

void test_optimize() {

    test_case cases[] = {
        {"5+5", 10},
        {"pow(2,2)", 4},
        {"sqrt 100", 10},
        {"pi * 2", 6.2832},
    };

    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        te_expr* ex = te_compile(expr, 0, 0, &err);
        lok(ex, expr);

        /* The answer should be know without
         * even running eval. */
        lfequal(ex->expr.value, answer, expr);
        lfequal(te_eval(ex), answer, expr);

        te_free(ex);
    }
}

void test_pow() {
#ifdef TE_POW_FROM_RIGHT
    test_equ cases[] = {
        {"2**3**4", "2**(3**4)"},
        {"-2**2", "-(2**2)"},
        {"--2**2", "(2**2)"},
        {"---2**2", "-(2**2)"},
        {"-(2*1)**2", "-(2**2)"},
        {"-2**2", "-4"},
        {"2**1.1**1.2**1.3", "2**(1.1**(1.2**1.3))"},
        {"-a**b", "-(a**b)"},
        {"-a**-b", "-(a**-b)"},
        {"1**0", "1"},
        {"(1)**0", "1"},
        {"-(2)**2", "-(2**2)"},
        /* TODO POW FROM RIGHT IS STILL BUGGY */
        {"(-2)**2", "4"},
        {"(-1)**0", "1"},
        {"(-5)**0", "1"},
        {"-2**-3**-4", "-(2**(-(3**-4)))"},
		{"-2**-3**-4", "-(2**(-(3**(-4))))"},
	};
#else
    test_equ cases[] = {
        {"2**3**4", "(2**3)**4"},
        {"-2**2", "(-2)**2"},
        {"(-2)**2", "4"},
        {"--2**2", "2**2"},
        {"---2**2", "(-2)**2"},
        {"-2**2", "4"},
        {"2**1.1**1.2**1.3", "((2**1.1)**1.2)**1.3"},
        {"-a**b", "(-a)**b"},
        {"-a**-b", "(-a)**(-b)"},
        {"1**0", "1"},
        {"(1)**0", "1"},
        {"(-1)**0", "1"},
        {"(-5)**0", "1"},
        {"-2**-3**-4", "((-2)**(-3))**(-4)"}
    };
#endif

    double a = 2, b = 3;

    te_variable lookup[] = {
        {"a", {.variable = &a}},
        {"b", {.variable = &b}}
    };

    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr1 = cases[i].expr1;
        const char* expr2 = cases[i].expr2;

        te_expr* ex1 = te_compile(expr1, lookup, nelem(lookup), 0);
        te_expr* ex2 = te_compile(expr2, lookup, nelem(lookup), 0);

        lok(ex1, expr1);
        lok(ex2, expr2);

        double r1 = te_eval(ex1);
        double r2 = te_eval(ex2);

        fflush(stdout);
        const int olfail = lfails;
        lfequal(r1, r2, "(see next report line:)");
        if (olfail != lfails) {
            printf("Failed expression: [%s] <> [%s] (%f <> %f)\n", expr1, expr2, r1, r2);
        }

        te_free(ex1);
        te_free(ex2);
    }

}

void test_combinatorics() {
    test_case cases[] = {
            {"fac(0)", 1},
            {"fac(0.2)", 1},
            {"fac(1)", 1},
            {"fac(2)", 2},
            {"fac(3)", 6},
            {"fac(4.8)", 85.621738 /* 24 */ },
            {"fac(10)", 3628800},

            {"ncr(0,0)", 1},
            {"ncr(10,1)", 10},
            {"ncr(10,0)", 1},
            {"ncr(10,10)", 1},
            {"ncr(16,7)", 11440},
            {"ncr(16,9)", 11440},
            {"ncr(100,95)", 75287520},

            {"npr(0,0)", 1},
            {"npr(10,1)", 10},
            {"npr(10,0)", 1},
            {"npr(10,10)", 3628800},
            {"npr(20,5)", 1860480},
            {"npr(100,4)", 94109400},
    };


    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        const double ev = te_interp(expr, &err);
        lok(!err, expr);
        lfequal(ev, answer, expr);

        if (err) {
            printf("FAILED: %s (%d)\n", expr, err);
        }
    }
}


void test_logic() {
    test_case cases[] = {
            {"1 && 1", 1},
            {"1 && 0", 0},
            {"0 && 1", 0},
            {"0 && 0", 0},
            {"1 || 1", 1},
            {"1 || 0", 1},
            {"0 || 1", 1},
            {"0 || 0", 0},
            {"!0", 1},
            {"!1", 0},
            {"!2", 0},

            {"!-2", 0},
            {"-!2", 0},
            {"!!0", 0},
            {"!!1", 1},
            {"!!2", 1},
            {"!!-2", 1},
            {"!-!2", 1},
            {"-!!2", -1},
            {"--!!2", 1},

            {"1 < 2", 1},
            {"2 < 2", 0},
            {"2 <= 2", 1},
            {"2 > 1", 1},
            {"2 > 2", 0},
            {"2 >= 2", 1},
            {"2 > -2", 1},
            {"-2 < 2", 1},

            {"0 == 0", 1},
            {"0 != 0", 0},
            {"2 == 2", 1},
            {"2 != 2", 0},
            {"2 == 3", 0},
            {"2 != 3", 1},
            {"2 == 2.0001", 0},
            {"2 != 2.0001", 1},

            {"1 < 2 && 2 < 3", 1},
            {"1 < 2 && 3 < 2", 0},
            {"2 < 1 && 2 < 3", 0},
            {"2 < 1 && 3 < 2", 0},
            {"1 < 2 || 2 < 3", 1},
            {"1 < 2 || 3 < 2", 1},
            {"2 < 1 || 2 < 3", 1},
            {"2 < 1 || 3 < 2", 0},

            {"1 < 1+1", 1},
            {"1 < 1*2", 1},
            {"1 < 2/2", 0},
            {"1 < 2**2", 1},

            {"5+5 < 4+10", 1},
            {"5+(5 < 4)+10", 15},
            {"5+(5 < 4+10)", 6},
            {"(5+5 < 4)+10", 10},
            {"5+!(5 < 4)+10", 16},
            {"5+!(5 < 4+10)", 5},
            {"!(5+5 < 4)+10", 11},

#ifdef TE_POW_FROM_RIGHT
            {"!0**2", 1},
            {"!0**-1", 0},
            {"-!0**2", -1},
#else
            {"!0**2", 1},
            {"!0**-1", 1},
            {"-!0**2", 1},
#endif

    };


    int i;
    for (i = 0; i < nelem(cases); ++i) {
        const char* expr = cases[i].expr;
        const double answer = cases[i].answer;

        int err;
        const double ev = te_interp(expr, &err);
        lok(!err, expr);
        lfequal(ev, answer, expr);

        if (err) {
            printf("FAILED: %s (%d)\n", expr, err);
        }
    }
}




void test_left_assoc() {
	test_case cases[] = {
		{"0 + 2 + 3 + 4 + 5 + 6", 20},
		{"0 - 2 - 3 - 4 - 5 - 6", -20},
		{"0 +- 2 +- 3 +- 4 +- 5 +- 6", -20},
		{"0 -+ 2 -+ 3 -+ 4 -+ 5 -+ 6", -20},
		{"0 -+- 2 -+- 3 -+- 4 -+- 5 -+- 6", 20},

		{"1 * 2 * 3 * 4 * 5 * 6", 2 * 3 * 4 * 5 * 6},
		{"-1 * -2 * -3 * -4 * -5 * -6", 2 * 3 * 4 * 5 * 6},
		{"+1 * +2 * +3 * +4 * +5 * +6", 2 * 3 * 4 * 5 * 6},
		{"720 / 2 / 3 / 4 / 5 / 6", 720 / 2 / 3 / 4 / 5 / 6},
	};


	int i;
	for (i = 0; i < nelem(cases); ++i) {
		const char* expr = cases[i].expr;
		const double answer = cases[i].answer;

		int err;
		const double ev = te_interp(expr, &err);
		lok(!err, expr);
		lfequal(ev, answer, expr);

		if (err) {
			printf("FAILED: %s (%d)\n", expr, err);
		}
	}
}



void test_right_assoc() {
	test_case cases[] = {
		{"2 ** 3 ** 4", pow(2, 9 * 9)},
	};


	int i;
	for (i = 0; i < nelem(cases); ++i) {
		const char* expr = cases[i].expr;
		const double answer = cases[i].answer;

		int err;
		const double ev = te_interp(expr, &err);
		lok(!err, expr);
		lfequal(ev, answer, expr);

		if (err) {
			printf("FAILED: %s (%d)\n", expr, err);
		}
	}
}





#if defined(BUILD_MONOLITHIC)
#define main      tiny_expr_smoke_main
#endif

int main(int argc, const char** argv)
{
	lrun("Pow", test_pow);
	lrun("Results", test_results);
    lrun("Syntax", test_syntax);
    lrun("NaNs", test_nans);
    lrun("INFs", test_infs);
	lrun("UnaryOpeerators", test_unary_ops);
	lrun("Variables #1", test_variables);
	lrun("Variables #2", test_variables2);
	lrun("Functions", test_functions);
    lrun("Dynamic", test_dynamic);
    lrun("Closure", test_closure);
    lrun("Optimize", test_optimize);
    lrun("Pow", test_pow);
    lrun("Combinatorics", test_combinatorics);
	lrun("Left Associativity", test_left_assoc);
	lrun("Right Associativity", test_right_assoc);
	lrun("Logic", test_logic);
    lresults();

    return lfails != 0;
}
