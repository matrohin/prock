option(ENABLE_TRACY "Enable Tracy profiler" OFF)

add_library(tracy STATIC ${CMAKE_CURRENT_LIST_DIR}/TracyClient.cpp)

target_include_directories(tracy PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}/public
)

if (ENABLE_TRACY)
  target_compile_definitions(tracy PUBLIC
    TRACY_ENABLE=1
  )

  target_compile_options(tracy PUBLIC
    -fno-omit-frame-pointer
  )

  target_link_options(tracy PUBLIC
    -rdynamic
  )

  target_link_libraries(tracy PUBLIC
    dl
    pthread
  )
endif ()