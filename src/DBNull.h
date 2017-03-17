#ifndef CHAPEAU_DBNULL_H
#define CHAPEAU_DBNULL_H

#include "DBAbstractDataType.h"
#include "Database.h"


class DBNull : public DBAbstractDataType {
   
public:
   DBNull(const std::string& correspondingType) :
      DBAbstractDataType(std::string("Null")),
      m_correspondingType(correspondingType) {
   }
   
   ~DBNull() {}
   
   void* value() const {
      return NULL;
   }
   
   std::string valueAsString() const {
      return std::string("<null>");
   }
   
   bool bind(Database* db, int idx, DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }
   
   const std::string& correspondingType() const {
      return m_correspondingType;
   }
   
   static std::string typeNameAsString() {
      return std::string("Null");
   }

private:
   std::string m_correspondingType;

};

#endif

