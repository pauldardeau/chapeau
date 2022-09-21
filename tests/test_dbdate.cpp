#include "test_dbdate.h"
#include "DBDate.h"

TestDBDate::TestDBDate() :
   TestSuite("TestDBDate") {
}

void TestDBDate::runTests() {
   test_default_constructor();
   test_constructor_with_double();
   test_constructor_with_datetime();
   test_constructor_with_string();
   test_copy_constructor();
   test_destructor();
   test_assignment_operator();
   test_getDateTime();
   test_timeIntervalSince1970();
   test_stringValue();
   test_valueAsString();
   test_bind();
   test_typeNameAsString();
}

void TestDBDate::test_default_constructor() {
   TEST_CASE("test_default_constructor");
}

void TestDBDate::test_constructor_with_double() {
   TEST_CASE("test_constructor_with_double");
}

void TestDBDate::test_constructor_with_datetime() {
   TEST_CASE("test_constructor_with_datetime");
}

void TestDBDate::test_constructor_with_string() {
   TEST_CASE("test_constructor_with_string");
}

void TestDBDate::test_copy_constructor() {
   TEST_CASE("test_copy_constructor");
}

void TestDBDate::test_destructor() {
   TEST_CASE("test_destructor");
}

void TestDBDate::test_assignment_operator() {
   TEST_CASE("test_assignment_operator");
}

void TestDBDate::test_getDateTime() {
   TEST_CASE("test_getDateTime");
}

void TestDBDate::test_timeIntervalSince1970() {
   TEST_CASE("test_timeIntervalSince1970");
}

void TestDBDate::test_stringValue() {
   TEST_CASE("test_stringValue");
}

void TestDBDate::test_valueAsString() {
   TEST_CASE("test_valueAsString");
}

void TestDBDate::test_bind() {
   TEST_CASE("test_bind");
}

void TestDBDate::test_typeNameAsString() {
   TEST_CASE("test_typeNameAsString");
}

