# ZenNode makefile for Linux

CC       = g++
CFLAGS   = -g -O6 -fstrength-reduce -funroll-loops -malign-jumps=4 -fno-exceptions -fno-rtti
WARNINGS = -Wall -Wno-format -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Winline
INCLUDES = -I../DOOM -I../common

.cpp.o:
	$(CC) -c $(CFLAGS) $(WARNINGS) $(INCLUDES) -o $@ $<

.SUFFIXES: .cpp .o

all: ZenNode ZenTest compare bspinfo

depend: clean

clean:
	rm -rf *.o ../DOOM/*.o

ZenNode: ZenMain.o ZenNode.o ZenReject.o blockmap.o console.o ../DOOM/wad.o ../DOOM/level.o
	$(CC) -o $@ $^

bspinfo: bspinfo.o console.o ../DOOM/wad.o ../DOOM/level.o
	$(CC) -o $@ $^

compare: compare.o console.o ../DOOM/wad.o ../DOOM/level.o
	$(CC) -o $@ $^
