#ifndef CHAPEAU_DBDATA_H
#define CHAPEAU_DBDATA_H

#include <vector>
#include <string.h>

#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

/**
 * A blob: a length and that many bytes. An empty DBData (length 0) is a
 * zero-length blob -- not SQL NULL, which is spelled DBNull or a null argument.
 */
class DBData : public DBAbstractDataType {

public:
   DBData() :
      DBAbstractDataType(std::string("Data")),
      m_length( 0L ) {
   }

   // Copies 'length' bytes from 'buffer'. A zero length, or no buffer at all,
   // gives an empty DBData; nothing is copied then (memcpy with a null pointer
   // is undefined even for zero bytes, and so is &v[0] of an empty vector).
   DBData(const void* buffer, unsigned long length) :
      DBAbstractDataType(std::string("Data")),
      m_length(0L) {
      if (buffer != nullptr && length > 0) {
         m_buffer.assign(static_cast<const char*>(buffer),
                         static_cast<const char*>(buffer) + length);
         m_length = length;
      }
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
      if (m_buffer.empty()) {
         return nullptr;
      }
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

