add_executable(
  power_panic_storage_tests ${CMAKE_SOURCE_DIR}/src/common/power_panic_storage_flash.cpp
                            power_panic_storage_test.cpp
  )
target_include_directories(
  power_panic_storage_tests
  PRIVATE # shadows the Marlin-heavy power_panic_storage.hpp
          ${CMAKE_CURRENT_SOURCE_DIR}/mock ${CMAKE_SOURCE_DIR}/src/version/include
  )
target_link_libraries(power_panic_storage_tests logging-mock w25x-mock)
add_catch_test(power_panic_storage_tests)

# The decisions of the power panic save and resume, without the Marlin state.
add_executable(
  power_panic_plan_tests ${CMAKE_SOURCE_DIR}/src/common/power_panic_plan.cpp
                         power_panic_plan_test.cpp
  )
target_include_directories(
  power_panic_plan_tests
  PRIVATE ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/lib/Marlin/Marlin/src
          ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src/common/utils
          ${CMAKE_SOURCE_DIR}/src/module/utils/buddy_utils
  )
# The firmware build has -Wextra (for example -Wmissing-field-initializers), so check the module
# with it here too.
target_compile_options(power_panic_plan_tests PRIVATE -Wall -Wextra -Werror)
add_catch_test(power_panic_plan_tests)
