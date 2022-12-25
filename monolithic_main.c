
#define BUILD_MONOLITHIC 1
#include "monolithic_examples.h"

#define USAGE_NAME   "tinyexpr_tests"

#include "monolithic_main_internal_defs.h"

MONOLITHIC_CMD_TABLE_START()

	{ "benchmark", {.fa = tiny_expr_benchmark_main } },
	{ "example1", {.f = tiny_expr_example1_main } },
	{ "example2", {.fa = tiny_expr_example2_main } },
	{ "example3", {.fa = tiny_expr_example3_main } },
	{ "repl", {.fa = tiny_expr_repl_main } },
	{ "smoke", {.fa = tiny_expr_smoke_main } },

MONOLITHIC_CMD_TABLE_END();

#include "monolithic_main_tpl.h"
