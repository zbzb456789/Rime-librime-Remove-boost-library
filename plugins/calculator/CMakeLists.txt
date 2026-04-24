project(rime-calculator)
cmake_minimum_required(VERSION 3.10)

set(SAMPLE_VERSION 1.0.0)
set(SAMPLE_SOVERSION 1)

aux_source_directory(src calculator_src)

set(calculator_library rime-calculator)
set(calculator_deps ${rime_library})
set(calculator_modules "calculator")

if(BUILD_SAMPLE)
  # to build the sample plugin, console app and tests, set BUILD_SAMPLE=ON
  set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

  message(STATUS "${calculator_library} provides modules: ${calculator_modules}")
  add_library(${calculator_library} ${calculator_src})
  target_link_libraries(${calculator_library} ${calculator_deps})
  set_target_properties(${calculator_library}
    PROPERTIES
    VERSION ${SAMPLE_VERSION}
    SOVERSION ${SAMPLE_SOVERSION})
  if(XCODE_VERSION)
    set_target_properties(${calculator_library}
      PROPERTIES
      INSTALL_NAME_DIR "@rpath")
  endif(XCODE_VERSION)
  install(TARGETS ${calculator_library} DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

  add_subdirectory(tools)
  if(BUILD_TEST)
    add_subdirectory(test)
  endif()
else()
  # to build as a standard Rime plugin, link or copy the directory to plugins/
  add_library(rime-calculator-objs OBJECT ${calculator_src})
  if(BUILD_SHARED_LIBS)
    set_target_properties(rime-calculator-objs
      PROPERTIES
      POSITION_INDEPENDENT_CODE ON)
  endif()

  set(plugin_name ${calculator_library} PARENT_SCOPE)
  set(plugin_objs $<TARGET_OBJECTS:rime-calculator-objs> PARENT_SCOPE)
  set(plugin_deps ${calculator_deps} PARENT_SCOPE)
  set(plugin_modules ${calculator_modules} PARENT_SCOPE)
endif()
