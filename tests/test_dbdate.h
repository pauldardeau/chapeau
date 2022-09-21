#ifndef TEST_DBDATE_H
#define TEST_DBDATE_H

#include <string>
#include "TestSuite.h"


class TestDBDate : public poivre::TestSuite {
protected:
   void runTests();

   void test_default_constructor();
   void test_constructor_with_double();
   void test_constructor_with_datetime();
   void test_constructor_with_string();
   void test_copy_constructor();
   void test_destructor();
   void test_assignment_operator();
   void test_getDateTime();
   void test_timeIntervalSince1970();
   void test_stringValue();
   void test_valueAsString();
   void test_bind();
   void test_typeNameAsString();

public:
   TestDBDate();
   
};


#endif

