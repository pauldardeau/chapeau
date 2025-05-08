#include <stdio.h>
#include <stdlib.h>
#include <string>

#include "SQLiteDatabase.h"
#include "SQLiteResultSet.h"

using namespace std;
using namespace chapeau;


int main(int argc, char** pszArgs)
{
   const int numArgs = argc - 1;
   if (numArgs < 2) {
      printf("usage: %s <db_file_name> <sql query>\n", pszArgs[0]);
      exit(1);
   }

   const char* dbFilePath = pszArgs[1];
   const char* dbQuery = pszArgs[2];
  
   SQLiteDatabase db(dbFilePath); 
   if (db.open()) {
      DBResultSet* rs = db.executeQuery(dbQuery);
      if (rs != NULL) {
         const int columnCount = rs->getColumnCount();
         while (rs->next()) {
            for (int i = 0; i < columnCount; i++) {
               if (i > 0) {
                  printf(",");
               }
               DBValueType dbValueType = rs->getTypeForColumnIndex(i);
	       if (dbValueType == ValueTypeInteger) {
	          printf("%d", rs->intForColumnIndex(i));
               } else if (dbValueType == ValueTypeFloat) {
                  printf("%f", rs->doubleForColumnIndex(i));
               } else if (dbValueType == ValueTypeText) {
                  string* s = rs->stringForColumnIndex(i);
                  if (s != NULL) {
                     printf("%s", s->c_str());
                     delete s;
                  }
               } else if (dbValueType == ValueTypeBlob) {
                  printf("<blob>");
               } else if (dbValueType == ValueTypeNull) {
                  printf("<null>");
               } else {
                  printf("???");
               }
	    }
	    printf("\n");
         }
	 rs->close();
	 delete rs;
      } else {
         printf("error: executeQuery returned null\n");
	 exit(1);
      }
   } else {
      printf("error: unable to open database '%s'\n", dbFilePath);
      exit(1);
   }
}
