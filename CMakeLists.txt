cmake_minimum_required(VERSION 3.16)

project(chapeau
   DESCRIPTION "C++ wrapper classes for the SQLite C library"
   LANGUAGES CXX
)

# True only when chapeau is configured directly, not pulled in via
# add_subdirectory() by a consumer. Controls whether chapeau's own
# test suite is built by default.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
   set(CHAPEAU_IS_TOP_LEVEL TRUE)
else()
   set(CHAPEAU_IS_TOP_LEVEL FALSE)
endif()

option(CHAPEAU_BUILD_TESTS "Build chapeau's own test suite" ${CHAPEAU_IS_TOP_LEVEL})

# chaudiere's own CHAUDIERE_BUILD_TESTS defaults off here (same
# CMAKE_SOURCE_DIR check, and this isn't chaudiere's own top-level
# configure), so this doesn't pull in chaudiere's tests or poivre.
add_subdirectory(chaudiere)
add_subdirectory(src)

if(CHAPEAU_BUILD_TESTS)
   add_subdirectory(poivre)
   enable_testing()
   add_subdirectory(tests)
endif()
