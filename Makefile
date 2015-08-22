CXXFLAGS+=-Wall -Wextra -Idoom -Icommon -D__LINUX__
TARGETS=ZenNode bspcomp bspdiff bspinfo
DOCS=ZenNode.1 bspcomp.1 bspdiff.1 bspinfo.1

ifdef WIN32
CXXFLAGS+=-D__WIN32__
endif

ifdef DEBUG
CXXFLAGS+=-DDEBUG -fexceptions
LOGGER=common/logger/logger.o common/logger/string.o common/logger/linux-logger.o
LIBS+=-lpthread -lrt
endif

.PHONY: all clean man install uninstall

# building the docs requires asciidoc
.SUFFIXES: .1 .adoc .html
.adoc.1:
	a2x -f manpage $<
.adoc.html:
	a2x -d manpage -f xhtml $<

all: $(TARGETS)
man: $(DOCS)

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
mandir?=share/man
target=$(DESTDIR)$(prefix)

install: $(TARGETS) $(DOCS)
	install -d $(target)/bin
	install -d $(target)/$(mandir)/man1
	install -m 755 $(TARGETS) $(target)/bin
	install -m 644 $(DOCS) $(target)/$(mandir)/man1

uninstall:
	for doc in $(DOCS); do rm $(target)/$(mandir)/man1/$$doc; done
	for bin in $(TARGETS); do rm $(target)/bin/$$bin; done
	-rmdir -p $(target)/$(mandir)/man1
	-rmdir -p $(target)/bin
