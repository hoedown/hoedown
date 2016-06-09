project (hoedown C)
cmake_minimum_required (VERSION 2.4)

option (ENABLE_TESTS "Enable test suites" TRUE)

find_program (GPERF gperf)
if (ENABLE_TESTS)
  find_package (PythonInterp)
  find_package (Perl)
  if (NOT PYTHONINTERP_FOUND AND NOT PERL_FOUND)
    message (FATAL_ERROR "Couldn't find neither Python or Perl")
  endif ()
  find_program (TIDY tidy)
  if (NOT TIDY)
    message (FATAL_ERROR "You need to have tidy to run tests")
  endif ()
endif ()

if (NOT LIB_SUFFIX)
  if (CMAKE_SIZEOF_VOID_P MATCHES "8")
    set (LIB_SUFFIX "64")
  endif ()
endif ()
if (NOT LIB_INSTALL_DIR)
  set (LIB_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib${LIB_SUFFIX})
endif ()

set (SRCS
     src/autolink.c
     src/buffer.c
     src/document.c
     src/escape.c
     src/html.c
     src/html_smartypants.c
     src/stack.c
     src/version.c)

if (GPERF)
  function (gperf_generate_new input output)
    add_custom_command (OUTPUT ${output}
                        COMMAND ${GPERF} ${ARGN} ${input} > ${output}
                        DEPENDS ${input}
                        COMMENT "Generate ${output}")
  endfunction ()
  gperf_generate_new (${CMAKE_CURRENT_SOURCE_DIR}/html_block_names.gperf
                      html_blocks.c
                      -L ANSI-C -N hoedown_find_block_tag -c -C -E -S 1 --ignore-case -m100)
  list (APPEND SRCS html_blocks.c)
else ()
  list (APPEND SRCS src/html_blocks.c)
endif ()

set (HDRS
     src/autolink.h
     src/buffer.h
     src/document.h
     src/escape.h
     src/html.h
     src/stack.h
     src/version.h)

add_library (libhoedown ${SRCS})
set_target_properties (libhoedown PROPERTIES
                       OUTPUT_NAME hoedown
                       SOVERSION 3)
include_directories (src)

add_executable (hoedown bin/hoedown.c)
target_link_libraries (hoedown libhoedown)

add_executable (smartypants bin/smartypants.c)
target_link_libraries (smartypants libhoedown)

install (TARGETS libhoedown hoedown smartypants
         RUNTIME DESTINATION bin
         LIBRARY DESTINATION ${LIB_INSTALL_DIR}
         ARCHIVE DESTINATION ${LIB_INSTALL_DIR})
install (FILES ${HDRS} DESTINATION include/hoedown)

if (ENABLE_TESTS)
  enable_testing ()
  if (PYTHONINTERP_FOUND)
    add_test (NAME test-py
              COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/runner.py)
    set_tests_properties(test-py PROPERTIES ENVIRONMENT HOEDOWN=./hoedown)
  endif ()
  if (PERL_FOUND)
    add_test (NAME test-pl
              COMMAND ${PERL_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/MarkdownTest_1.0.3/MarkdownTest.pl
                                           --script=./hoedown
                                           --testdir=${CMAKE_CURRENT_SOURCE_DIR}/test/MarkdownTest_1.0.3/Tests
                                           --tidy)
  endif ()
endif ()
