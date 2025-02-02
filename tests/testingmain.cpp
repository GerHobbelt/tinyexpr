#include "../tinyexpr.h"
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

int main(int argc, char** argv)
    {
    Catch::Session session;

    session.applyCommandLine(argc, argv);

    session.run();

    return EXIT_SUCCESS;
    }
