project(rime-SpellingFilter)
cmake_minimum_required(VERSION 3.10)

set(SAMPLE_VERSION 1.0.0)
set(SAMPLE_SOVERSION 1)

aux_source_directory(src SpellingFilter_src)

set(SpellingFilter_library rime-SpellingFilter)
set(SpellingFilter_deps ${rime_library})
set(SpellingFilter_modules "SpellingFilter")

if(BUILD_SAMPLE)
  # to build the sample plugin, console app and tests, set BUILD_SAMPLE=ON
  set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

  message(STATUS "${SpellingFilter_library} provides modules: ${SpellingFilter_modules}")
  add_library(${SpellingFilter_library} ${SpellingFilter_src})
  target_link_libraries(${SpellingFilter_library} ${SpellingFilter_deps})
  set_target_properties(${SpellingFilter_library}
    PROPERTIES
    VERSION ${SAMPLE_VERSION}
    SOVERSION ${SAMPLE_SOVERSION})
  if(XCODE_VERSION)
    set_target_properties(${SpellingFilter_library}
      PROPERTIES
      INSTALL_NAME_DIR "@rpath")
  endif(XCODE_VERSION)
  install(TARGETS ${SpellingFilter_library} DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})

  add_subdirectory(tools)
  if(BUILD_TEST)
    add_subdirectory(test)
  endif()
else()
  # to build as a standard Rime plugin, link or copy the directory to plugins/
  add_library(rime-SpellingFilter-objs OBJECT ${SpellingFilter_src})
  if(BUILD_SHARED_LIBS)
    set_target_properties(rime-SpellingFilter-objs
      PROPERTIES
      POSITION_INDEPENDENT_CODE ON)
  endif()

  set(plugin_name ${SpellingFilter_library} PARENT_SCOPE)
  set(plugin_objs $<TARGET_OBJECTS:rime-SpellingFilter-objs> PARENT_SCOPE)
  set(plugin_deps ${SpellingFilter_deps} PARENT_SCOPE)
  set(plugin_modules ${SpellingFilter_modules} PARENT_SCOPE)
endif()
