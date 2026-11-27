# Host tests for the control code of the modular bed puppy (XL). The tests compile the real control
# files, the register map and the Modbus protocol code. fake_hal.cpp replaces the ADC, the PWM
# timer, the GPIO signals, the microsecond clock, the OTP and the RTOS (tick and mail queue). The
# headers in include/ replace the RTOS, the watchdog and the task priorities.
set(MODULARBED_DIR ${CMAKE_SOURCE_DIR}/src/puppy/modularbed)

add_executable(
  modularbed_control_tests
  ${MODULARBED_DIR}/control/AutoconfLogic.cpp
  ${MODULARBED_DIR}/control/ControlLogic.cpp
  ${MODULARBED_DIR}/control/ControlTask.cpp
  ${MODULARBED_DIR}/control/HeatbedletInfo.cpp
  ${MODULARBED_DIR}/control/IIR1Filter.cpp
  ${MODULARBED_DIR}/control/MeasurementCalculator.cpp
  ${MODULARBED_DIR}/control/MeasurementLogic.cpp
  ${MODULARBED_DIR}/control/MovingAverageFilter.cpp
  ${MODULARBED_DIR}/control/PWMLogic.cpp
  ${MODULARBED_DIR}/control/StateLogic.cpp
  ${MODULARBED_DIR}/ModbusRegisters.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/shared/modbus/ModbusBuffer.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/shared/modbus/ModbusProtocol.cpp
  ${CMAKE_SOURCE_DIR}/src/puppy/shared/modbus/ModbusRegisterDictionary.cpp
  fake_hal.cpp
  test_support.cpp
  control_tests.cpp
  pwm_tests.cpp
  )
target_include_directories(
  modularbed_control_tests
  PRIVATE include
          ${MODULARBED_DIR}
          ${MODULARBED_DIR}/control
          ${CMAKE_SOURCE_DIR}/src/puppy/shared
          ${CMAKE_SOURCE_DIR}/src/puppy/shared/modbus
          ${CMAKE_SOURCE_DIR}/src/common/include
          ${CMAKE_SOURCE_DIR}/src/common/utils
          ${CMAKE_SOURCE_DIR}/src/common
          ${CMAKE_SOURCE_DIR}/include
  )
# PuppyConfig.hpp selects the bedlet layout by printer. The unit tests are configured as MINI, so
# replace the printer type with the XL (3).
target_compile_options(modularbed_control_tests PRIVATE -UPRINTER_TYPE -DPRINTER_TYPE=3)
target_link_libraries(modularbed_control_tests otp byte_utils)
add_catch_test(modularbed_control_tests)
