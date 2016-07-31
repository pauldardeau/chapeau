#ifndef CHAPEAU_DBBOOL_H
#define CHAPEAU_DBBOOL_H

#include "DBAbstractDataType.h"
#include "Database.h"

class DBBool : public DBAbstractDataType {
   
public:
   DBBool(bool boolValue) :
      DBAbstractDataType(std::string("Bool")),
      m_value(boolValue) {
   }
   
   DBBool(const DBBool& copy) :
      DBAbstractDataType(copy),
      m_value(copy.m_value) {
   }
   
   ~DBBool() {
   }
   
   DBBool& operator=(const DBBool& copy ) {
      if (this == &copy) {
         return *this;
      }
      
      DBAbstractDataType::operator=(copy);
      m_value = copy.m_value;
      
      return *this;
   }
   
   bool value() const {
      return m_value;
   }
   
   std::string valueAsString() const {
      if (m_value) {
         return "true";
      } else {
         return "false";
      }
   }
   
   bool bind(Database* db,
             int idx,
             DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Bool");
   }

   
private:
   bool m_value;
};

#endif

