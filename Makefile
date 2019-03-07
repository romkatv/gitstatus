APPNAME ?= gitstatusd

CXX ?= g++

CXXFLAGS += -std=c++14 -funsigned-char -O3 -DNDEBUG -Wall -Werror
LDFLAGS += -s -static-libstdc++ -static-libgcc

CXXFLAGS += $(shell pkg-config --cflags libgit2)
LDFLAGS += $(shell pkg-config --libs-only-L libgit2)

LDLIBS ?= -l:libgit2.a

SRCS := $(shell find src -name "*.cc")
OBJS := $(patsubst src/%.cc, obj/%.o, $(SRCS))

all: $(APPNAME)

$(APPNAME): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) $(LDLIBS) -o $(APPNAME)

obj:
	mkdir -p obj

obj/%.o: src/%.cc Makefile | obj
	$(CXX) $(CXXFLAGS) -MM -MT $@ src/$*.cc >obj/$*.dep
	$(CXX) $(CXXFLAGS) -Wall -c -o $@ src/$*.cc

clean:
	rm -rf obj

-include $(OBJS:.o=.dep)
