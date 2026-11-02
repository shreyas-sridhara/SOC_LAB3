# Makefile for Lab 3 part 3 (DotAccel: SystemC/TLM model + standalone testbench)
#
#   make                 build ./tb_dot_accel
#   make run             all tests, interrupt synchronisation, default timing

SYSTEMC_HOME ?= /usr/local/packages/systemc-2.3.4
CXXSTD       ?=

# SystemC installs its library in lib/ or lib-linux64/ depending on the build method
SC_LIBDIR := $(firstword $(wildcard $(SYSTEMC_HOME)/lib-linux64 $(SYSTEMC_HOME)/lib))

# -O0 -g           : debuggable
# -ffp-contract=off: no fused multiply-add, keeps float math bit-exact vs llama.cpp
CXXFLAGS ?= -g -O0 -Wall -ffp-contract=off
CXXFLAGS += $(CXXSTD) -I$(SYSTEMC_HOME)/include
LDFLAGS  += -L$(SC_LIBDIR) -Wl,-rpath,$(SC_LIBDIR)
LDLIBS   += -lsystemc -lpthread

TARGET = tb_dot_accel
OBJS   = tb_dot_accel.o dot_q8_0_core.o vec_dot_q8_0.o
HDRS   = dot_accel.h dot_accel_regs.h dot_q8_0_core.h vec_dot_q8_0.h

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# reference code is plain C
vec_dot_q8_0.o: vec_dot_q8_0.c vec_dot_q8_0.h
	$(CC) -g -O0 -Wall -ffp-contract=off -c $< -o $@

run: $(TARGET)
	./$(TARGET) -v 0

run-poll: $(TARGET)
	./$(TARGET) -v 0 --poll

run-all: run run-poll

trace: $(TARGET)
	./$(TARGET) -v 2 --only "random nb=1"

vcd: $(TARGET)
	./$(TARGET) -v 1 --vcd --only "random nb=1"
	@echo "wrote dot_accel.vcd  ->  gtkwave dot_accel.vcd"

debug: $(TARGET)
	gdb -ex "break DotAccel::compute_process" -ex "run -v 3 --only 'random nb=1'" ./$(TARGET)

memcheck: $(TARGET)
	valgrind --leak-check=full ./$(TARGET) -v 0

clean:
	rm -f $(TARGET) tb_platform_bus *.o *.vcd

# platform-level test without QEMU; LIBSOC = <systemctlm-cosim-demo>/libsystemctlm-soc
LIBSOC ?= $(HOME)/systemctlm-cosim-demo/libsystemctlm-soc
tb_platform_bus: tb_platform_bus.cpp dot_q8_0_core.o dot_golden.h $(HDRS)
	$(CXX) $(CXXFLAGS) -I$(LIBSOC) -o $@ $< dot_q8_0_core.o $(LDFLAGS) $(LDLIBS)

bus-test: tb_platform_bus
	./tb_platform_bus -v 0

.PHONY: all run run-poll run-all trace vcd debug memcheck clean bus-test
