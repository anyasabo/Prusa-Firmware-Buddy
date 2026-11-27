set(INDX_HEAD_SRC ${CMAKE_SOURCE_DIR}/src/puppy/indx_head/src)

add_executable(indx_head_temperature_tests indx_head_temperature_tests.cpp)
target_include_directories(indx_head_temperature_tests PUBLIC ${INDX_HEAD_SRC})
add_catch_test(indx_head_temperature_tests)

# Host tests of the pure heater logic: the power control (heater_control.cpp) and the ringdown
# analysis (ringdown.cpp). heater.cpp keeps the hardware access.
add_executable(
  indx_head_heater_tests ${INDX_HEAD_SRC}/heater_control.cpp ${INDX_HEAD_SRC}/ringdown.cpp
                         heater_control_tests.cpp ringdown_tests.cpp
  )
target_include_directories(
  indx_head_heater_tests PRIVATE ${INDX_HEAD_SRC} ${CMAKE_SOURCE_DIR}/src/module/indx_head/include
  )
target_link_libraries(indx_head_heater_tests fpm)
add_catch_test(indx_head_heater_tests)
