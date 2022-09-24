#ifndef CHAPEAU_DBDATA_H
#define CHAPEAU_DBDATA_H

#include <vector>
#include <string.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

class DBData : public DBAbstractDataType {

public:
   DBData() :
      DBAbstractDataType(std::string("Data")),
      m_length( 0L ) {
   }

   DBData(const void* buffer, unsigned long length) :
      DBAbstractDataType(std::string("Data")),
      m_buffer(length),
      m_length(length) {
      ::memcpy(&m_buffer[0], buffer, m_length);
   }

   DBData(const DBData& copy) :
      DBAbstractDataType(copy),
      m_buffer(copy.m_buffer),
      m_length( copy.m_length ) {
   }

   ~DBData() {}

   DBData& operator=(const DBData& copy) {
      if (this == &copy) {
         return *this;
      }

      DBAbstractDataType::operator=(copy);

      m_buffer = copy.m_buffer;
      m_length = copy.m_length;

      return *this;
   }

   unsigned long length() const {
      return m_length;
   }

   const void* value() const {
      return (const void*) &m_buffer[0];
   }

   std::string valueAsString() const {
      return std::string("<data>");
   }

   bool bind(Database* db,
             int idx,
             DBStatement* statement) const {
      return db->bind(this, idx, statement);
   }

   static std::string typeNameAsString() {
      return std::string("Data");
   }


private:
   std::vector<char> m_buffer;
   unsigned long m_length;
};

}

#endif

