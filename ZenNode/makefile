# ZenNode makefile for Linux

CC       = g++
CFLAGS   = -g -O3 -fstrength-reduce -funroll-loops -malign-jumps=4 -fno-exceptions -fno-rtti
WARNINGS = -Wall -Wno-format -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Winline
INCLUDES = -I../DOOM -I../common
TARGETS  = ZenNode compare bspinfo

ifdef WIN32
CFLAGS  += -D__WIN32__
endif

ifdef DEBUG
CFLAGS  += -DDEBUG
LOGGER   = ../common/logger/logger.o ../common/logger/string.o
endif

.cpp.o:
	$(CC) -c $(CFLAGS) $(WARNINGS) $(INCLUDES) -o $@ $<

.SUFFIXES: .cpp .o

all: $(TARGETS)

depend: clean

clean:
	rm -rf {.,../DOOM/,../common/logger}/{*.o,*~} $(TARGETS)

ZenNode: ZenMain.o ZenNode.o ZenReject.o blockmap.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CC) -o $@ $^

bspinfo: bspinfo.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CC) -o $@ $^

compare: compare.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CC) -o $@ $^
