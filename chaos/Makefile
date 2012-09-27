#
#
# $Id: Makefile 71 2006-07-11 17:09:47Z brad $
#

#---------- figure out what we are runnign on ---------

OS_NAME = $(shell uname)
MACH_NAME = $(shell uname -m)

ifeq ($(OS_NAME), Darwin)
OS = OSX
endif

ifeq ($(OS_NAME), Linux)
OS = LINUX
endif

#----------- code ------------

CHAOSD_OBJ = chaosd.o transport.o node.o log.o signal.o

SERVER_OBJ = server.o chaos.o ncp.o rfc.o testpackets.o log.o signal.o

CFLAGS = -g -fno-builtin-log

# if 64 bit
ifeq ($(MACH_NAME), x86_64)
#CFLAGS += -m32
endif


all: chaosd listen server client time FILE

chaosd: $(CHAOSD_OBJ)
	$(CC) $(CFLAGS) -o chaosd $(CHAOSD_OBJ)

listen: listen.c
	$(CC) $(CFLAGS) -o listen listen.c

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o server $(SERVER_OBJ)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

time: time.c
	$(CC) $(CFLAGS) -o time time.c

FILE: FILE.c FILE.h glob.c chlib.c
	$(CC) $(CFLAGS) -o FILE FILE.c glob.c chlib.c

clean:
	rm -f *.o chaosd listen server FILE time client

