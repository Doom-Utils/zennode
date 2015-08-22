CXXFLAGS += -Wall -Wextra -Idoom -Icommon -D__LINUX__
TARGETS   = ZenNode bspcomp bspdiff bspinfo

ifdef WIN32
CXXFLAGS  += -D__WIN32__
endif

ifdef DEBUG
CXXFLAGS += -DDEBUG -fexceptions
LOGGER    = common/logger/logger.o common/logger/string.o common/logger/linux-logger.o
LIBS     += -lpthread -lrt
endif

.PHONY: all clean install uninstall

all: $(TARGETS)

ZenNode:					\
  doom/level.o					\
  doom/wad.o					\
  src/ZenMain.o					\
  src/ZenNode.o					\
  src/ZenRMB.o					\
  src/ZenReject.o				\
  src/blockmap.o				\
  src/console.o					\
  $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspdiff:					\
  doom/level.o					\
  doom/wad.o					\
  src/bspdiff.o					\
  src/console.o					\
  $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspinfo:					\
  doom/level.o					\
  doom/wad.o					\
  src/bspinfo.o					\
  src/console.o					\
  $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

bspcomp:					\
  doom/level.o					\
  doom/wad.o					\
  src/bspcomp.o					\
  src/console.o					\
  $(LOGGER)
	$(CXX) $(LIBS) -o $@ $^

clean:
	rm -f */*.o $(TARGETS)

prefix?=/usr/local
target=$(DESTDIR)$(prefix)

install: $(TARGETS)
	install -d $(target)/bin
	install -m 755 ZenNode bspcomp bspdiff bspinfo $(target)/bin

uninstall:
	rm $(target)/bin/ZenNode $(target)/bin/bspcomp
	rm $(target)/bin/bspdiff $(target)/bin/bspinfo
	-rmdir -p $(target)/bin
