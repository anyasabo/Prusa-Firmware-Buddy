target_link_libraries(firmware PUBLIC xbuddy_extension)

if(XBUDDY_EXTENSION_VARIANT_IS_STANDARD)
  target_sources(firmware PRIVATE chamber_alarm.cpp cooling.cpp xbuddy_extension.cpp)
endif()

if(XBUDDY_EXTENSION_VARIANT_IS_iX)
  target_sources(firmware PRIVATE xbuddy_extension.cpp)
endif()
