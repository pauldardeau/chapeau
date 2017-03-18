#ifndef CHAPEAU_DBABSTRACTDATATYPE_H
#define CHAPEAU_DBABSTRACTDATATYPE_H

#include <string>
#include <memory>


namespace chapeau {

class Database;
class DBStatement;

/**
 *
 */
class DBAbstractDataType {
   
public:
   /**
    * Constructs an instance with a data type name
    * @param the name of the concrete data type
    */
   explicit DBAbstractDataType(std::string typeName) :
      m_typeName(typeName) {
   }
   
   /**
    * Copy constructor
    * @param copy the source of the copy
    */
   DBAbstractDataType(const DBAbstractDataType& copy) :
      m_typeName(copy.m_typeName) {
   }
   
   /**
    * Destructor
    */
   virtual ~DBAbstractDataType() {}
   
   /**
    * Copy operator
    * @param copy the source of the copy
    * @return target of the copy
    */
   DBAbstractDataType& operator=(const DBAbstractDataType& copy) {
      if (this == &copy) {
         return *this;
      }
      
      m_typeName = copy.m_typeName;
      
      return *this;
   }
   
   /**
    * Retrieves the data type name
    * @return data type name
    */
   const std::string& typeName() const {
      return m_typeName;
   }
   
   /**
    * Retrieves the value as a string
    * @return value as a string
    */
   virtual std::string valueAsString() const = 0;
   
   /**
    * Binds the value to a database statement parameter
    * @param db the database connection
    * @param idx index of the parameter to bind
    * @param statement the statement whose parameter is being bound
    * @return boolean indicating whether the bind was successful
    * @see Database()
    * @see DBStatement()
    */
   virtual bool bind(Database* db,
                     int idx,
                     DBStatement* statement) const = 0;


private:
   std::string m_typeName;

};

}

#endif

