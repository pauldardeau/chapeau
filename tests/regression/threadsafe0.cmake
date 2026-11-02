# The SQLITE_THREADSAFE=0 test (H1a-12b). Included by CMakeLists.txt; OFF unless asked.
#
# Chapeau refuses to open a database when the SQLite it runs on was compiled
# without threading support. The only honest test of that is a SQLite that really
# was, so this builds one program that contains Chapeau's sources AND a pinned
# SQLite amalgamation compiled with -DSQLITE_THREADSAFE=0, and runs it through
# the same bounded-subprocess harness as the other scenarios.
#
#   cmake -S . -B build-ts0 -DCHAPEAU_THREADSAFE0_TEST=ON
#   cmake --build build-ts0
#   ctest --test-dir build-ts0 -L sqlite-threading
#
# Where the amalgamation comes from (never in an ordinary build):
#   * CHAPEAU_THREADSAFE0_SQLITE_DIR=<dir>  an unpacked sqlite-amalgamation-3370200
#     (sqlite3.c and sqlite3.h). No network. Its sqlite3.c and sqlite3.h are
#     checked against the pinned SHA-256 values below.
#   * otherwise it is downloaded from sqlite.org by FetchContent, and the
#     archive is checked against the pinned SHA-256 below; a changed or corrupted
#     download stops the configure step.
#
# The pin. Obtained from https://www.sqlite.org/2022/sqlite-amalgamation-3370200.zip
# (SQLite 3.37.2, source id 2022-01-06 13:25:41 872ba256cbf61d9290b571c0e6d82a20c224ca3ad82971edc46b29818d5d17a0),
# the same release as the libsqlite3 of Ubuntu 22.04 that the ordinary test
# environment uses (which differs from it only by a distribution suffix on the
# source id). Raising the version means changing the four values below together.

option(CHAPEAU_THREADSAFE0_TEST
   "Build and run the test against a SQLite compiled with SQLITE_THREADSAFE=0 (needs the pinned amalgamation: see threadsafe0.cmake)"
   OFF)
set(CHAPEAU_THREADSAFE0_SQLITE_DIR "" CACHE PATH
   "Unpacked sqlite-amalgamation-3370200 directory for CHAPEAU_THREADSAFE0_TEST (downloaded if empty)")

if(NOT CHAPEAU_THREADSAFE0_TEST)
   return()
endif()

set(CHAPEAU_TS0_VERSION       "3.37.2")
set(CHAPEAU_TS0_URL           "https://www.sqlite.org/2022/sqlite-amalgamation-3370200.zip")
set(CHAPEAU_TS0_ZIP_SHA256    "cb25df0fb90b77be6660f6ace641bbea88f3d0441110d394ce418f35f7561bb0")
set(CHAPEAU_TS0_C_SHA256      "f316e712031efa690161e2f717d78c4a683953974a4608a87be8d922b16ebf94")
set(CHAPEAU_TS0_H_SHA256      "c0d27886483ecc4527fc06cacaa1ee4a2766cc57c17e100864dac0f26edcd79c")

if(CHAPEAU_THREADSAFE0_SQLITE_DIR)
   set(ts0Dir "${CHAPEAU_THREADSAFE0_SQLITE_DIR}")
else()
   include(FetchContent)
   FetchContent_Declare(chapeau_sqlite_threadsafe0
      URL "${CHAPEAU_TS0_URL}"
      URL_HASH "SHA256=${CHAPEAU_TS0_ZIP_SHA256}")
   FetchContent_MakeAvailable(chapeau_sqlite_threadsafe0)
   set(ts0Dir "${chapeau_sqlite_threadsafe0_SOURCE_DIR}")
endif()

foreach(pair "sqlite3.c;${CHAPEAU_TS0_C_SHA256}" "sqlite3.h;${CHAPEAU_TS0_H_SHA256}")
   list(GET pair 0 name)
   list(GET pair 1 expected)
   if(NOT EXISTS "${ts0Dir}/${name}")
      message(FATAL_ERROR "chapeau: ${ts0Dir}/${name} not found (CHAPEAU_THREADSAFE0_TEST)")
   endif()
   file(SHA256 "${ts0Dir}/${name}" actual)
   if(NOT actual STREQUAL expected)
      message(FATAL_ERROR "chapeau: ${ts0Dir}/${name} is not the pinned SQLite ${CHAPEAU_TS0_VERSION} amalgamation\n"
                          "  expected SHA-256 ${expected}\n  actual   SHA-256 ${actual}")
   endif()
endforeach()

# the amalgamation is C
enable_language(C)

# Chapeau's own sources are compiled into each program (not the chapeau library
# target, which links the system SQLite) so that they run on the pinned SQLite.
set(ts0ChapeauSources
   Database.cpp
   DBDate.cpp
   DBStatementArgs.cpp
   SQLiteDatabase.cpp
   SQLiteResultSet.cpp)
list(TRANSFORM ts0ChapeauSources PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../../src/")

# chapeau_add_pinned_sqlite_probe(<target> <source> <SQLITE_THREADSAFE value>)
# One program: <source> + Chapeau's sources + the pinned sqlite3.c compiled with
# that threading mode. The amalgamation is its own object library so that it can
# be built once per mode. No extension loading (it would need libdl), and no
# warnings reported for third-party code (-w, on that file only).
function(chapeau_add_pinned_sqlite_probe target source mode)
   add_library(${target}_sqlite OBJECT "${ts0Dir}/sqlite3.c")
   target_compile_definitions(${target}_sqlite PRIVATE
      SQLITE_THREADSAFE=${mode}
      SQLITE_OMIT_LOAD_EXTENSION=1)
   target_compile_options(${target}_sqlite PRIVATE -w)

   add_executable(${target} ${source} ${ts0ChapeauSources} $<TARGET_OBJECTS:${target}_sqlite>)
   target_compile_features(${target} PRIVATE cxx_std_20)
   set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
   target_compile_definitions(${target} PRIVATE EXPECTED_SQLITE_VERSION="${CHAPEAU_TS0_VERSION}")
   # the amalgamation's sqlite3.h first, so that Chapeau's "sqlite3.h" is this one
   target_include_directories(${target} BEFORE PRIVATE
      "${ts0Dir}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
      "${CMAKE_CURRENT_SOURCE_DIR}/..")
   target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra>)
   # Chaudiere (logging and string/date helpers); it does not link SQLite
   target_link_libraries(${target} PRIVATE chaudiere)
endfunction()

# SQLITE_THREADSAFE=0: open() must refuse it
chapeau_add_pinned_sqlite_probe(chapeau_threadsafe0_probe threadsafe0_probe.cpp 0)
foreach(scenario
   threadsafe0_library_is_what_the_build_says
   threadsafe0_open_is_refused
   threadsafe0_repeated_refusal_leaves_the_object_valid
   threadsafe0_refusal_is_logged_once_and_silent_when_off)
   chapeau_add_case(regression_${scenario} chapeau_threadsafe0_probe ${scenario}
      TIMEOUT 60 LABEL regression)
   set_property(TEST regression_${scenario} APPEND PROPERTY LABELS sqlite-threading)
endforeach()

# SQLITE_THREADSAFE=2 (multi-thread): open() must accept it
chapeau_add_pinned_sqlite_probe(chapeau_threadsafe2_probe threadsafe2_probe.cpp 2)
foreach(scenario
   threadsafe2_library_is_what_the_build_says
   threadsafe2_open_is_allowed)
   chapeau_add_case(regression_${scenario} chapeau_threadsafe2_probe ${scenario}
      TIMEOUT 60 LABEL regression)
   set_property(TEST regression_${scenario} APPEND PROPERTY LABELS sqlite-threading)
endforeach()
