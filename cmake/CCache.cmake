if(NOT DEFINED NO_CCACHE)
  find_program(CCACHE ccache OPTIONAL)

  if(CCACHE STREQUAL "CCACHE-NOTFOUND")
    message(STATUS "CCache not found")
  else()
    message(STATUS "CCache found: ${CCACHE}")

    # No time_macros sloppiness: with it, ccache ignores __DATE__ (lib/Marlin/Marlin/src/Marlin.cpp)
    # and returns an object with the date of an earlier build, also when SOURCE_DATE_EPOCH changed.
    # Without it, ccache keys that object on the date or on SOURCE_DATE_EPOCH. The cost: Marlin.cpp
    # misses the cache once a day if SOURCE_DATE_EPOCH is not set.
    set(CMAKE_CXX_COMPILER_LAUNCHER
        "${CCACHE};sloppiness=pch_defines;namespace=${PRINTER}-${CMAKE_BUILD_TYPE}-${BOOTLOADER};base_dir=${CMAKE_SOURCE_DIR};absolute_paths_in_stderr=true;ignore_options=--specs=nano.specs --specs=nosys.specs"
        )
    set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CXX_COMPILER_LAUNCHER}")
  endif()
else()
  message(STATUS "CCache disabled")
endif()
