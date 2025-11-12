# Copyright Paul Dardeau, SwampBits LLC 2014
# BSD License

CC = cc
CXX = c++
CC_OPTS = -c -fPIC -Wall -O2
CXX_OPTS = -c -Wall -std=c++20 -fPIC -O2 -I/usr/local/include -I../chaudiere/src
ARCHIVE_CMD = ar
ARCHIVE_OPTS = rs

LIB_NAME = libchapeau.so
EXE_NAME = query

EXE_OBJS = query.o

LIB_OBJS = Database.o \
DBDate.o \
DBStatementArgs.o \
SQLiteDatabase.o \
SQLiteResultSet.o

all : $(LIB_NAME) $(EXE_NAME)

clean :
	rm -f *.o
	rm -f $(LIB_NAME)
	rm -f $(EXE_NAME)

$(LIB_NAME) : $(LIB_OBJS)
	$(CXX) -shared -fPIC $(LIB_OBJS) -o $(LIB_NAME) ../chaudiere/src/libchaudiere.so -L/usr/local/lib -lsqlite3

$(EXE_NAME) : $(EXE_OBJS)
	$(CXX) $(EXE_OBJS) -o $(EXE_NAME) ./libchapeau.so ../chaudiere/src/libchaudiere.so -L/usr/local/lib -lsqlite3 -lpthread

%.o : %.cpp
	$(CXX) $(CXX_OPTS) $< -o $@

%.o : %.c
	$(CC) $(CC_OPTS) $< -o $@
