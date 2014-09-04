cmake_minimum_required (VERSION 2.6)
project (Hoedown)

option (BUILD_SHARED_LIBS "Wether to build Hoedown as a shared library" true)
option (BUILD_CLI "Wether to build the hoedown and smartypants executables" true)

set (FLAGS "-std=c99 -Wall -Wextra -Wno-unused-parameter")


# Pregenerated files
add_custom_target(html_blocks.c
  gperf -L ANSI-C -N hoedown_find_block_tag -c -C -E -S 1 --ignore-case -m100 html_block_names.gperf > src/html_blocks.c
)


# Library
add_library(libhoedown
  src/autolink.c
  src/buffer.c
  src/document.c
  src/escape.c
  src/html.c
  src/html_blocks.c
  src/html_smartypants.c
  src/stack.c
  src/version.c
)

set_target_properties(libhoedown PROPERTIES
  COMPILE_FLAGS ${FLAGS}
  OUTPUT_NAME hoedown
  SOVERSION 2
  VERSION 2.0.0
)

install(TARGETS libhoedown DESTINATION lib)
install(DIRECTORY src/ DESTINATION include/hoedown
  FILES_MATCHING PATTERN "*.h"
)


# Executables
if (BUILD_CLI)
  add_executable(hoedown bin/hoedown.c)
  target_link_libraries(hoedown libhoedown)

  add_executable(smartypants bin/smartypants.c)
  target_link_libraries(smartypants libhoedown)

  set_target_properties(hoedown smartypants PROPERTIES COMPILE_FLAGS ${FLAGS})
  include_directories(${PROJECT_SOURCE_DIR}/src)

  install(TARGETS hoedown smartypants DESTINATION bin)

  get_target_property(HOEDOWN_LOCATION hoedown LOCATION)
  add_custom_target(test
    test/runner.sh ${HOEDOWN_LOCATION} test/MarkdownTest_1.0.3/Tests
    DEPENDS hoedown
  )
endif (BUILD_CLI)
