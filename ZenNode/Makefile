CXXFLAGS += -Wall -Wextra -I../DOOM -I../common -D__LINUX__
TARGETS   = ZenNode bspcomp bspdiff bspinfo

ifdef WIN32
CXXFLAGS  += -D__WIN32__
endif

ifdef DEBUG
CXXFLAGS  += -DDEBUG -fexceptions
LOGGER   = ../common/logger/logger.o ../common/logger/string.o ../common/logger/linux-logger.o
LIBS    += -lpthread -lrt
endif

.PHONY: all clean install uninstall

all: $(TARGETS)

clean:
	rm -rf {.,../DOOM/,../common/logger}/{*.o,*~} $(TARGETS)

ZenNode: ZenMain.o ZenNode.o ZenReject.o ZenRMB.o blockmap.o \
  console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspdiff: bspdiff.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspinfo: bspinfo.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspcomp: bspcomp.o console.o ../DOOM/wad.o ../DOOM/level.o $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

prefix?=/usr/local
target=$(DESTDIR)$(prefix)

install: $(TARGETS)
	install -d $(target)/bin
	install -m 755 ZenNode bspcomp bspdiff bspinfo $(target)/bin

uninstall:
	rm $(target)/bin/ZenNode $(target)/bin/bspcomp
	rm $(target)/bin/bspdiff $(target)/bin/bspinfo
	-rmdir -p $(target)/bin
