# Native test of the VCV host with the real plug-in (no Rack needed).
#   make -C vcv/test         run the checks
#   make -C vcv/test render  host-screens.png (menu screens)
NT      := ../..
ROOT    := ../../..
include $(NT)/vcv/sources.mk

CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-unused-function $(PERSEIDS_MATH) $(PERSEIDS_DEFS) $(PERSEIDS_INCS)
CFLAGS   := -std=gnu11 -O2 $(PERSEIDS_MATH) $(PERSEIDS_DEFS) $(PERSEIDS_INCS) -Wno-unused

OBJ := $(patsubst %,build/%.o,$(notdir $(PERSEIDS_CXX) $(PERSEIDS_C)))
vpath %.cpp $(sort $(dir $(PERSEIDS_CXX)))
vpath %.c   $(sort $(dir $(PERSEIDS_C)))

all: host_test
	./host_test > frames.txt

build/%.cpp.o: %.cpp
	@mkdir -p build
	g++ $(CXXFLAGS) -MMD -MP -c -o $@ $<
build/%.c.o: %.c
	@mkdir -p build
	gcc $(CFLAGS) -c -o $@ $<

host_test: host_test.cpp $(OBJ)
	g++ $(CXXFLAGS) -o $@ $^

render: all
	python3 $(NT)/render/render.py frames.txt host-screens.png screens

clean:
	rm -rf build host_test frames.txt

.PHONY: all render clean
-include $(wildcard build/*.d)
