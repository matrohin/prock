option(ENABLE_TRACY "Enable Tracy profiler" OFF)

if (ENABLE_TRACY)
  add_library(tracy STATIC ${CMAKE_CURRENT_LIST_DIR}/TracyClient.cpp)
  target_include_directories(tracy PUBLIC ${CMAKE_CURRENT_LIST_DIR}/public)

  target_compile_definitions(tracy PUBLIC TRACY_ENABLE=1)
  target_compile_options(tracy PUBLIC -fno-omit-frame-pointer)

  target_link_options(tracy PUBLIC -rdynamic)
  target_link_libraries(tracy PUBLIC dl pthread)
else()
  add_library(tracy INTERFACE)
  target_include_directories(tracy INTERFACE ${CMAKE_CURRENT_LIST_DIR}/public)
endif ()