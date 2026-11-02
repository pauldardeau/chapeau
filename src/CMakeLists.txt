# Static by default (respects BUILD_SHARED_LIBS), same convention as
# poivre/chaudiere/misere/tonnerre. Doesn't affect the Makefile-built
# libchapeau.so.
add_library(chapeau
   Database.cpp
   DBDate.cpp
   DBStatementArgs.cpp
   SQLiteDatabase.cpp
   SQLiteResultSet.cpp
   UnitState.cpp
)

target_compile_features(chapeau PUBLIC cxx_std_20)
set_target_properties(chapeau PROPERTIES CXX_EXTENSIONS OFF)

target_compile_options(chapeau PRIVATE
   $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)

# Headers live at this directory's root (no include/ subdirectory), so
# this is the one directory a consumer needs on its include path.
target_include_directories(chapeau
   PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
      $<INSTALL_INTERFACE:include/chapeau>
)

# PUBLIC: chapeau's own public headers use chaudiere types (logging,
# string utilities, date/time per the README), so a consumer of chapeau
# also needs chaudiere's include directory and link library. No poivre
# dependency at all in src/ (verified: no AutoPointer/poivre:: usage),
# unlike chaudiere/misere.
target_link_libraries(chapeau PUBLIC chaudiere)

# CMake's built-in FindSQLite3 module (3.14+) - more portable than the
# Makefile's hardcoded -L/usr/local/lib -lsqlite3 (works with a system
# package, pkg-config, vcpkg, etc. without editing anything here).
find_package(SQLite3 REQUIRED)
target_link_libraries(chapeau PUBLIC SQLite::SQLite3)

include(GNUInstallDirs)

install(TARGETS chapeau
   EXPORT chapeauTargets
   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

file(GLOB CHAPEAU_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
# UnitState.h is internal: Database.h and SQLiteResultSet.h only forward-declare UnitState
list(FILTER CHAPEAU_HEADERS EXCLUDE REGEX "/UnitState\\.h$")
install(FILES ${CHAPEAU_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chapeau)

# the query demo executable
add_executable(query query.cpp)
target_link_libraries(query PRIVATE chapeau)

install(TARGETS query RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
