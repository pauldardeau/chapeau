#ifndef CHAPEAU_DBSTRING_H
#define CHAPEAU_DBSTRING_H

#include <string>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

class DBString : public DBAbstractDataType {
   
public:
   DBString(const std::string& value) :
      DBAbstractDataType(std::string("String")),
      m_value(value) {
   }
   
   DBString(const DBString& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }
   
   ~DBString() {}
   
   DBString& operator=(const DBString& copy) {
      if (this == &copy) {
         return *this;
      }
      
      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;
      
      return *this;
   }
   
   const std::string& value() const {
      return m_value;
   }
   
   std::string valueAsString() const {
      return std::string(m_value);
   }

   bool bind(Database* db, int idx, DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }
   
   static std::string typeNameAsString() {
      return std::string("String");
   }
   
private:
   std::string m_value;

};

}

#endif

