#ifndef CHAPEAU_DBSTATEMENTARGS_H
#define CHAPEAU_DBSTATEMENTARGS_H

#include <vector>


namespace chapeau {

class DBAbstractDataType;


/**
 *
 */
class DBStatementArgs {

public:
   /**
    *
    */
   DBStatementArgs();

   /**
    * Destructor
    */
   virtual ~DBStatementArgs();

   /**
    *
    */
   void clear();

   /**
    *
    * @return
    */
   std::size_t size() const;

   /**
    *
    * @return
    */
   bool empty() const;

   /**
    *
    * @param arg
    */
   void add(DBAbstractDataType* arg);

   /**
    *
    * @return
    */
   DBAbstractDataType* operator[](std::size_t pos);

   /**
    *
    * @return
    */
   const DBAbstractDataType* operator[](std::size_t pos) const;


private:
   std::vector<DBAbstractDataType*> m_args;

   // no copies
   DBStatementArgs(const DBStatementArgs&);
   DBStatementArgs& operator=(const DBStatementArgs&);

};

}

#endif

