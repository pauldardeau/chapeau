#include "test_dbdate.h"
#include "DBDate.h"
#include "DateTime.h"

using namespace std;
using namespace chapeau;
using namespace chaudiere;


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
   DBDate d;
   DateTime dt;
   double d_value = d.timeIntervalSince1970();
   double dt_value = dt.timeIntervalSince1970();
   double delta = dt_value - d_value;
   require(delta < 0.0001, "unix time from DBDate should match that from DateTime");
}

void TestDBDate::test_constructor_with_double() {
   TEST_CASE("test_constructor_with_double");
   DateTime dt;
   double dt_unix_time = dt.timeIntervalSince1970();
   DBDate d(dt_unix_time);
   double d_unix_time = d.timeIntervalSince1970();
   double delta = d_unix_time - dt_unix_time;
   require(delta < 0.0001, "DBDate constructed with unix time should match DateTime");
}

void TestDBDate::test_constructor_with_datetime() {
   TEST_CASE("test_constructor_with_datetime");
   DateTime dt;
   DBDate d(dt);
   string dt_string = dt.formattedString();
   string d_string = d.stringValue();
   requireStringEquals(dt_string, d_string, "DBDate constructed with DateTime should agree with its value");
}

void TestDBDate::test_constructor_with_string() {
   TEST_CASE("test_constructor_with_string");
   // yyyymmddhhmmss
   string ts = "20220922091633";
   DBDate d(ts);
   // yyyy-mm-dd hh:mm:ss
   string ts_formatted = d.stringValue();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_copy_constructor() {
   TEST_CASE("test_copy_constructor");
   string ts = "20220922091633";
   DBDate d1(ts);
   DBDate d2(d1);
   string ts_formatted = d2.stringValue();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_destructor() {
   TEST_CASE("test_destructor");
   DBDate d1;
}

void TestDBDate::test_assignment_operator() {
   TEST_CASE("test_assignment_operator");
   string ts = "20220922091633";
   DBDate d1(ts);
   DBDate d2;
   d2 = d1;
   string ts_formatted = d2.stringValue();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_getDateTime() {
   TEST_CASE("test_getDateTime");
   string ts = "20220922091633";
   DBDate d(ts);
   const DateTime& dt = d.getDateTime();
   string ts_formatted = dt.formattedString();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_timeIntervalSince1970() {
   TEST_CASE("test_timeIntervalSince1970");
   string ts = "20220922091633";
   DBDate d(ts);
   DateTime dt(ts);
   double d_unix_time = d.timeIntervalSince1970();
   double dt_unix_time = dt.timeIntervalSince1970();
   double delta = dt_unix_time - d_unix_time;
   require(delta < 0.00001, "unix time for DBDate should match that for DateTime");
}

void TestDBDate::test_stringValue() {
   TEST_CASE("test_stringValue");
   string ts = "20220922091633";
   DBDate d(ts);
   string ts_formatted = d.stringValue();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_valueAsString() {
   TEST_CASE("test_valueAsString");
   string ts = "20220922091633";
   DBDate d(ts);
   string ts_formatted = d.valueAsString();
   requireStringEquals("2022-09-22 09:16:33.000000", ts_formatted);
}

void TestDBDate::test_bind() {
   TEST_CASE("test_bind");
   //TODO: implement test_bind
}

void TestDBDate::test_typeNameAsString() {
   TEST_CASE("test_typeNameAsString");
   string type_name = DBDate::typeNameAsString();
   requireStringEquals("Date", type_name);
}

