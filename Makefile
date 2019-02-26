appname := gitstatus

CXX := g++

CXXFLAGS :=       \
  -std=c++17      \
  -funsigned-char \
  -fmax-errors=1  \
  -O3             \
  -g              \
  -DNDEBUG

LDFLAGS :=          \
  -static-libstdc++ \
  -static-libgcc    \
  -pthread          \
  -l:libgit2.a      \
  -l:libssl.a       \
  -l:libcrypto.a    \
  -l:libz.a         \
  -ldl

SRCS := $(shell find src -name "*.cc")
OBJS := $(patsubst src/%.cc, obj/%.o, $(SRCS))

all: $(appname)

$(appname): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $(appname)

obj:
	mkdir -p obj

obj/%.o: src/%.cc Makefile | obj
	$(CXX) $(CXXFLAGS) -MM -MT $@ src/$*.cc >obj/$*.dep
	$(CXX) $(CXXFLAGS) -Wall -c -o $@ src/$*.cc

clean:
	rm -rf obj

-include $(OBJS:.o=.dep)
