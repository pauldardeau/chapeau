#ifndef CHAPEAU_LONG_H
#define CHAPEAU_LONG_H

#include <cstdint>
#include <string>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

/**
 * A SQLite INTEGER argument, 64 bits wide on every platform. (It used to hold a
 * C++ long, which is 32 bits on Windows: a value above 2^31 - 1 was silently
 * truncated before it reached SQLite. The name is unchanged.)
 */
class DBLong : public DBAbstractDataType {

public:
   DBLong(std::int64_t value) :
      DBAbstractDataType(std::string("Long")),
      m_value(value) {
   }

   DBLong(const DBLong& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }

   ~DBLong() {}

   DBLong& operator=(const DBLong& copy) {
      if (this == &copy) {
         return *this;
      }

      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;

      return *this;
   }

   std::int64_t value() const {
      return m_value;
   }

   std::string valueAsString() const {
      // (a fixed 20-byte buffer could not hold INT64_MIN, 20 characters, and its NUL)
      return std::to_string(static_cast<long long>(m_value));
   }

   bool bind(Database* db, int idx, DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Long");
   }

private:
   std::int64_t m_value;

};

}

#endif
