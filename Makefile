APPNAME ?= gitstatusd

CXX ?= g++

CXXFLAGS += -std=c++14 -funsigned-char -O3 -DNDEBUG -Wall -Werror -fsized-deallocation # -g -fsanitize=thread
LDFLAGS += -pthread # -fsanitize=thread
LDLIBS += -lgit2 # -lprofiler -lunwind

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
