target_sources(
  firmware
  PRIVATE calibrated_pressure_advance.cpp gcode_pressure_advance_calibration.cpp
          pressure_advance_calibration.cpp screen_pressure_advance_calibration.cpp
          step_response_analyzer.cpp
  )
