# ZenNode makefile for Linux

CC := g++
OBJS := ZenMain.o ZenNode.o ZenReject.o blockmap.o ../DOOM/wad.o ../DOOM/level.o
CFLAGS := -g -O6 -fstrength-reduce -funroll-loops -malign-jumps=2 -march=pentiumpro -fno-exceptions -fno-rtti
WARNINGS := -Wall -Wno-format -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Winline
INCLUDES := -I../DOOM -I../common

.cpp.o:
	@$(CC) -c $(CFLAGS) $(WARNINGS) $(INCLUDES) -o $@ $<

.SUFFIXES: .cpp .o

all: ZenNode

depend: clean

clean:
	@rm -rf *.o ../DOOM/*.o

ZenNode: $(OBJS)
	@$(CC) -o $@ $^

bspinfo: bspinfo.o ../DOOM/wad.o ../DOOM/level.o
	@$(CC) -o $@ $^

compare: compare.o ../DOOM/wad.o ../DOOM/level.o
	@$(CC) -o $@ $^
