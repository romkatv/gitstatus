APPNAME ?= gitstatusd

CXX ?= g++

# Note: -fsized-deallocation is not used to avoid binary compatibility issues on macOS.
#
# Sized delete is implemented as __ZdlPvm in /usr/lib/libc++.1.dylib but this symbol is
# missing in macOS prior to 10.13.
CXXFLAGS += -std=c++14 -funsigned-char -O3 -DNDEBUG -Wall -Werror # -g -fsanitize=thread
LDFLAGS += -pthread # -fsanitize=thread
LDLIBS += -lgit2 # -lprofiler -lunwind

SRCS := $(shell find src -name "*.cc")
OBJS := $(patsubst src/%.cc, obj/%.o, $(SRCS))

all: $(APPNAME)

$(APPNAME): usrbin/$(APPNAME)

usrbin/$(APPNAME): $(OBJS) | usrbin
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $@

usrbin:
	mkdir -p usrbin

obj:
	mkdir -p obj

obj/%.o: src/%.cc Makefile | obj
	$(CXX) $(CXXFLAGS) -MM -MT $@ src/$*.cc >obj/$*.dep
	$(CXX) $(CXXFLAGS) -Wall -c -o $@ src/$*.cc

clean:
	rm -rf obj

-include $(OBJS:.o=.dep)
