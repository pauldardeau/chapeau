# Copyright Paul Dardeau, SwampBits LLC 2014
# BSD License

CC = gcc
CXX = g++
CC_OPTS = -c -Wall -O2
CXX_OPTS = -c -Wall -O2 -I../../chaudiere/src
ARCHIVE_CMD = ar
ARCHIVE_OPTS = rs

LIB_NAME = libchapeau.a

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
	$(ARCHIVE_CMD) $(ARCHIVE_OPTS) $(LIB_NAME) $(OBJS)

%.o : %.cpp
	$(CXX) $(CXX_OPTS) $< -o $@

%.o : %.c
	$(CC) $(CC_OPTS) $< -o $@
