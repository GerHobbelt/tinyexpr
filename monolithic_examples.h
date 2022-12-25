
#pragma once

#if defined(BUILD_MONOLITHIC)

#ifdef __cplusplus
extern "C" {
#endif

	int tiny_expr_benchmark_main(int argc, const char** argv);
	int tiny_expr_example1_main(void);
	int tiny_expr_example3_main(int argc, const char** argv);
	int tiny_expr_example2_main(int argc, const char** argv);
	int tiny_expr_repl_main(int argc, const char** argv);
	int tiny_expr_smoke_main(int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif
