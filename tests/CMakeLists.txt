# Unlike the Makefile (which recompiles chapeau's src/*.cpp files a
# second time directly into the test binary), this links the real
# chapeau library target - same test coverage, no source duplication.
add_executable(test_chapeau
   fs_test_case.cpp
   test_database.cpp
   test_dbdate.cpp
   test_db_statement_args.cpp
   test_sqlite_database.cpp
   test_sqlite_foundation.cpp
   test_sqlite_result_set.cpp
   test_sqlite_statement.cpp
   tests.cpp
)

# chaudiere and SQLite::SQLite3 both come in transitively via chapeau's
# own PUBLIC links to them.
target_link_libraries(test_chapeau PRIVATE chapeau poivre)

add_test(
   NAME test_chapeau
   COMMAND test_chapeau
   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# subprocess regression harness: crash/hang/sanitizer-aware cases, and the
# known-defect reproductions (opt-in, see regression/CMakeLists.txt)
add_subdirectory(regression)
