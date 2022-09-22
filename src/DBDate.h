#ifndef CHAPEAU_DBDATE_H
#define CHAPEAU_DBDATE_H

#include "DateTime.h"
#include "DBAbstractDataType.h"
#include "Database.h"


namespace chapeau {

/**
 *
 */
class DBDate : public DBAbstractDataType {
   
public:
   /**
    * Default constructor
    */
   DBDate();
   
   /**
    *
    * @param timeIntervalSince1970
    */
   DBDate(double timeIntervalSince1970);
   
   /**
    *
    * @param dateTime
    * @see chaudiere::DateTime()
    */
   DBDate(const chaudiere::DateTime& dateTime);
   
   /**
    *
    * @param dateAsString
    */
   DBDate(const std::string& dateAsString);
   
   /**
    * Copy constructor
    * @param copy the source of the copy
    */
   DBDate(const DBDate& copy);
   
   /**
    * Destructor
    */
   ~DBDate();
   
   /**
    * Copy operator
    * @param copy the source of the copy
    * @return target of the copy
    */
   DBDate& operator=(const DBDate& copy);

   /**
    *
    * @return
    * @see chaudiere::DateTime()
    */
   const chaudiere::DateTime& getDateTime() const;
   
   /**
    *
    * @return
    */
   double timeIntervalSince1970() const;
   
   /**
    *
    * @return
    */
   std::string stringValue() const;
   
   /**
    *
    * @return
    */
   std::string valueAsString() const;

   /**
    *
    * @param db
    * @param idx
    * @param statement
    * @return
    * @see Database()
    * @see DBStatement()
    */
   bool bind(Database* db, int idx, DBStatement* statement) const;

   /**
    *
    * @return
    */
   static std::string typeNameAsString();

   
private:
   chaudiere::DateTime m_dateTime;

};

}

#endif

