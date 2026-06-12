#include <catch2/catch_test_macros.hpp>

#include "core/ModuleInfo.h"

#include <string>

TEST_CASE("tsq_core test binary runs", "[core]")
{
    REQUIRE(std::string{tsq::core::moduleName()} == "tsq_core");
}
