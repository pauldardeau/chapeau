CC = c++
CC_OPTS = -c -std=c++20 -I../src -I../poivre -I../chaudiere/src -I/usr/local/include

EXE_NAME = test_chapeau
LIB_NAMES = ../chaudiere/src/libchaudiere.so -L/usr/local/lib -lsqlite3 -lpthread

PROJ_OBJS = ../src/Database.o \
../src/DBDate.o \
../src/DBStatementArgs.o \
../src/SQLiteDatabase.o \
../src/SQLiteResultSet.o \
../src/UnitState.o

POIVRE_OBJS = TestCase.o \
TestSuite.o \
TestRegistry.o

OBJS = fs_test_case.o \
test_database.o \
test_dbdate.o \
test_db_statement_args.o \
test_sqlite_database.o \
test_sqlite_foundation.o \
test_sqlite_result_set.o \
test_sqlite_statement.o \
test_unit_state.o \
test_unit_claims.o \
tests.o \
$(POIVRE_OBJS)

all : $(EXE_NAME)

clean :
	rm -f *.o
	rm -f $(EXE_NAME)

$(EXE_NAME) : $(OBJS)
	$(CC) $(OBJS) $(PROJ_OBJS) -o $(EXE_NAME) $(LIB_NAMES)

$(POIVRE_OBJS) : %.o : ../poivre/%.cpp
	$(CC) $(CC_OPTS) $< -o $@

%.o : %.cpp
	$(CC) $(CC_OPTS) $< -o $@
