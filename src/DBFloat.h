#ifndef CHAPEAU_FLOAT_H
#define CHAPEAU_FLOAT_H

#include <stdio.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

/**
 * A float to bind. SQLite stores floating point values as doubles, so the float
 * is promoted to the double with exactly the same value and that is what is
 * stored and read back: DBFloat(0.1f) holds 0.100000001490116..., not 0.1.
 * Nothing is lost, but it is not the double nearest to the decimal 0.1 either,
 * so a comparison such as WHERE x = 0.1 does not match it. To store the
 * precision of a double (or of a decimal literal) use DBDouble.
 * Binding a NaN stores SQL NULL, as SQLite does.
 */
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

