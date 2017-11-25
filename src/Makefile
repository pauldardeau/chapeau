# Copyright Paul Dardeau, SwampBits LLC 2014
# BSD License

CC = cc
CXX = c++
CC_OPTS = -c -shared -fPIC -Wall -O2
CXX_OPTS = -c -Wall -shared -fPIC -O2 -I/usr/local/include -I../../chaudiere/src
ARCHIVE_CMD = ar
ARCHIVE_OPTS = rs

LIB_NAME = chapeau.so

OBJS = Database.o \
DBDate.o \
DBStatementArgs.o \
SQLiteDatabase.o \
SQLiteResultSet.o

all : $(LIB_NAME)

clean :
	rm -f *.o
	rm -f $(LIB_NAME)
	
$(LIB_NAME) : $(OBJS)
	$(CXX) -shared -fPIC $(OBJS) -o $(LIB_NAME)

%.o : %.cpp
	$(CXX) $(CXX_OPTS) $< -o $@

%.o : %.c
	$(CC) $(CC_OPTS) $< -o $@
