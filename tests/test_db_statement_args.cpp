#include "test_db_statement_args.h"
#include "DBStatementArgs.h"
#include "DBBool.h"
#include "DBData.h"
#include "DBDate.h"
#include "DBDouble.h"
#include "DBFloat.h"
#include "DBInt.h"
#include "DBLong.h"
#include "DBNull.h"
#include "DBString.h"

using namespace chapeau;


TestDBStatementArgs::TestDBStatementArgs() :
   TestSuite("TestDBStatementArgs") {
}

void TestDBStatementArgs::runTests() {
   test_constructor();
   test_destructor();
   test_clear();
   test_size();
   test_empty();
   test_add();
   test_non_const_operator_brackets();
   test_const_operator_brackets();
}

void TestDBStatementArgs::test_constructor() {
   TEST_CASE("test_constructor");
   DBStatementArgs args;
}

void TestDBStatementArgs::test_destructor() {
   TEST_CASE("test_destructor");
   DBStatementArgs args;
   args.add(new DBInt(5));
}

void TestDBStatementArgs::test_clear() {
   TEST_CASE("test_clear");
   DBStatementArgs args;
   require(args.size() == 0, "initial size must be 0");
   args.add(new DBInt(5));
   args.add(new DBString("foo"));
   require(args.size() == 2, "size must be 2 after adding 2");
   args.clear();
   require(args.size() == 0, "size must be 0 after clear");
}

void TestDBStatementArgs::test_size() {
   TEST_CASE("test_size");
   DBStatementArgs args;
   require(args.size() == 0);
   args.add(new DBInt(5));
   args.add(new DBString("foo"));
   require(args.size() == 2);
}

void TestDBStatementArgs::test_empty() {
   TEST_CASE("test_empty");
   DBStatementArgs args;
   require(args.size() == 0);
   require(args.empty());
   args.add(new DBInt(5));
   requireFalse(args.empty());
}

void TestDBStatementArgs::test_add() {
   TEST_CASE("test_add");
   DBStatementArgs args;
   require(args.size() == 0);
   args.add(new DBInt(5));
   args.add(new DBString("foo"));
   args.add(new DBDouble(3.124));
   args.add(new DBLong(100L));
   args.add(new DBFloat(1.2));
   chaudiere::DateTime dt;
   args.add(new DBDate(dt));
   char buffer[4096];
   memset(buffer, 0, sizeof(buffer));
   args.add(new DBData(buffer, 4096));
   require(args.size() == 7);
}

void TestDBStatementArgs::test_non_const_operator_brackets() {
   TEST_CASE("test_non_const_operator_brackets");
   DBStatementArgs args;
   require(args.size() == 0);
   DBInt* db_int = new DBInt(5);
   DBString* db_string = new DBString("foo");
   DBDouble* db_double = new DBDouble(3.124);
   DBLong* db_long = new DBLong(100L);
   DBFloat* db_float = new DBFloat(1.2);
   chaudiere::DateTime dt;
   DBDate* db_date = new DBDate(dt);
   char buffer[4096];
   memset(buffer, 0, sizeof(buffer));
   DBData* db_data = new DBData(buffer, 4096);
   DBNull* db_null = new DBNull("string");

   args.add(db_int);
   args.add(db_string);
   args.add(db_double);
   args.add(db_long);
   args.add(db_float);
   args.add(db_date);
   args.add(db_data);
   args.add(db_null);
   require(args.size() == 8);

   require(args[0] == db_int);
   require(args[1] == db_string);
   require(args[2] == db_double);
   require(args[3] == db_long);
   require(args[4] == db_float);
   require(args[5] == db_date);
   require(args[6] == db_data);
   require(args[7] == db_null);
}

void TestDBStatementArgs::test_const_operator_brackets() {
   TEST_CASE("test_const_operator_brackets");
   DBStatementArgs args;
   require(args.size() == 0);
   DBInt* db_int = new DBInt(5);
   DBString* db_string = new DBString("foo");
   DBDouble* db_double = new DBDouble(3.124);
   DBLong* db_long = new DBLong(100L);
   DBFloat* db_float = new DBFloat(1.2);
   chaudiere::DateTime dt;
   DBDate* db_date = new DBDate(dt);
   char buffer[4096];
   memset(buffer, 0, sizeof(buffer));
   DBData* db_data = new DBData(buffer, 4096);
   DBNull* db_null = new DBNull("string");

   args.add(db_int);
   args.add(db_string);
   args.add(db_double);
   args.add(db_long);
   args.add(db_float);
   args.add(db_date);
   args.add(db_data);
   args.add(db_null);
   require(args.size() == 8);

   const DBStatementArgs& c_args = args;

   require(c_args[0] == db_int);
   require(c_args[1] == db_string);
   require(c_args[2] == db_double);
   require(c_args[3] == db_long);
   require(c_args[4] == db_float);
   require(c_args[5] == db_date);
   require(c_args[6] == db_data);
   require(c_args[7] == db_null);
}

