# CMake generated Testfile for 
# Source directory: /Users/dawneweisman/Dev/Theory/tests
# Build directory: /Users/dawneweisman/Dev/Theory/out/build/debug/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[tsq_core_tests]=] "/Users/dawneweisman/Dev/Theory/out/build/debug/tests/tsq_core_tests")
set_tests_properties([=[tsq_core_tests]=] PROPERTIES  LABELS "unit;pure;headless" _BACKTRACE_TRIPLES "/Users/dawneweisman/Dev/Theory/tests/CMakeLists.txt;33;add_test;/Users/dawneweisman/Dev/Theory/tests/CMakeLists.txt;0;")
add_test([=[tsq_engine_integration_tests]=] "/Users/dawneweisman/Dev/Theory/out/build/debug/tests/tsq_engine_integration_tests")
set_tests_properties([=[tsq_engine_integration_tests]=] PROPERTIES  LABELS "integration;engine;plugin;vst3" _BACKTRACE_TRIPLES "/Users/dawneweisman/Dev/Theory/tests/CMakeLists.txt;111;add_test;/Users/dawneweisman/Dev/Theory/tests/CMakeLists.txt;0;")
