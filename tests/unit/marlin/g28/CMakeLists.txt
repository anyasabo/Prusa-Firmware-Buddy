# Host tests of the pure G28 decisions: the homing plan (g28_plan.cpp) and the precise CoreXY
# refinement policy (corexy_refine_policy.cpp). G28.cpp keeps the side effects.
set(MARLIN_SRC ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src)

add_executable(
  g28_tests
  ${MARLIN_SRC}/gcode/calibrate/g28_plan.cpp ${MARLIN_SRC}/gcode/calibrate/corexy_refine_policy.cpp
  g28_plan_tests.cpp corexy_refine_policy_tests.cpp
  )
target_include_directories(g28_tests PRIVATE ${MARLIN_SRC} ${CMAKE_SOURCE_DIR}/src/common)
add_catch_test(g28_tests)
