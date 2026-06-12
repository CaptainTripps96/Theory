#include <catch2/catch_test_macros.hpp>

#include "core/diagnostics/Logger.h"
#include "core/diagnostics/Result.h"

#include <string>

TEST_CASE("log levels have stable display names", "[diagnostics]")
{
    using tsq::core::diagnostics::LogLevel;
    using tsq::core::diagnostics::logLevelName;

    REQUIRE (std::string { logLevelName (LogLevel::debug) } == "debug");
    REQUIRE (std::string { logLevelName (LogLevel::info) } == "info");
    REQUIRE (std::string { logLevelName (LogLevel::warning) } == "warning");
    REQUIRE (std::string { logLevelName (LogLevel::error) } == "error");
}

TEST_CASE("logger stores and formats entries", "[diagnostics]")
{
    using tsq::core::diagnostics::Logger;
    using tsq::core::diagnostics::LogLevel;

    Logger logger { Logger::ConsoleOutput::disabled };

    logger.info ("App started");
    logger.warning ("Careful");

    const auto entries = logger.entries();
    REQUIRE (entries.size() == 2);
    REQUIRE (entries[0].level == LogLevel::info);
    REQUIRE (entries[0].message == "App started");
    REQUIRE (entries[1].level == LogLevel::warning);
    REQUIRE (entries[1].message == "Careful");

    const auto formatted = logger.formattedEntries();
    REQUIRE (formatted.size() == 2);
    REQUIRE (formatted[0] == "[info] App started");
    REQUIRE (formatted[1] == "[warning] Careful");
}

TEST_CASE("diagnostic result reports success and failure consistently", "[diagnostics]")
{
    using tsq::core::diagnostics::Result;

    const auto success = Result::success();
    REQUIRE (success.succeeded());
    REQUIRE_FALSE (success.failed());
    REQUIRE (success.error().empty());

    const auto failure = Result::failure ("Something went wrong");
    REQUIRE_FALSE (failure.succeeded());
    REQUIRE (failure.failed());
    REQUIRE (failure.error() == "Something went wrong");
}
