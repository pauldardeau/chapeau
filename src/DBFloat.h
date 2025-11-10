#ifndef CHAPEAU_FLOAT_H
#define CHAPEAU_FLOAT_H

#include <stdio.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

class DBFloat : public DBAbstractDataType {

public:
   DBFloat(float value) :
      DBAbstractDataType(std::string("Float")),
      m_value(value) {
   }

   DBFloat(const DBFloat& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }

   ~DBFloat() {}

   DBFloat& operator=(const DBFloat& copy ) {
      if (this == &copy) {
         return *this;
      }

      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;

      return *this;
   }

   float value() const {
      return m_value;
   }

   std::string valueAsString() const {
      char buffer[20];
      snprintf(buffer, 20, "%f", m_value);
      return std::string(buffer);
   }

   bool bind(Database* db, int idx, DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Float");
   }

private:
   float m_value;

};

}

#endif

