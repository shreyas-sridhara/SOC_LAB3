#define SC_INCLUDE_DYNAMIC_PROCESSES

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "systemc.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/tlm_quantumkeeper.h"

using namespace sc_core;
using namespace sc_dt;
using namespace std;

#include "soc/interconnect/iconnect.h"
#include "tests/test-modules/memory.h"
#include "debugdev.h"
#include "soc/xilinx/zynqmp/xilinx-zynqmp.h"

#include "dot_accel.h"          
                                

#define NR_MASTERS   1          
#define NR_DEVICES   3          

SC_MODULE(Top)
{
    SC_HAS_PROCESS(Top);

    iconnect<NR_MASTERS, NR_DEVICES> bus;
    xilinx_zynqmp zynq;
    memory mem;                 
    debugdev debug;             
    DotAccel dot;               

    sc_signal<bool> rst, rst_n;

    void pull_reset(void)
    {
        rst.write(true);
        wait(1, SC_US);
        rst.write(false);
    }

    void gen_rst_n(void)
    {
        rst_n.write(!rst.read());
    }

    Top(sc_module_name name, const char *sk_descr, sc_time quantum) :
        sc_module(name),
        bus("bus"),
        zynq("zynq", sk_descr),
        mem("mem", sc_time(1, SC_NS), 64 * 1024),
        debug("debug"),
        dot("dot"),
        rst("rst"),
        rst_n("rst_n")
    {
        SC_METHOD(gen_rst_n);
        sensitive << rst;

        m_qk.set_global_quantum(quantum);

        zynq.rst(rst);

        bus.memmap(0xa0000000ULL, 0x100 - 1,
                   ADDRMODE_RELATIVE, -1, debug.socket);
        bus.memmap(DOT_BASE_ADDR, DOT_WINDOW_SIZE - 1,
                   ADDRMODE_RELATIVE, -1, dot.socket);
        bus.memmap(0xa0800000ULL, 64 * 1024 - 1,
                   ADDRMODE_RELATIVE, -1, mem.socket);

        zynq.s_axi_hpm_fpd[0]->bind(*(bus.t_sk[0]));

        debug.irq[0](zynq.pl2ps_irq[0]);
        dot.irq(zynq.pl2ps_irq[DOT_PL2PS_IRQ]);

        zynq.tie_off();

        printf("zynqmp_dot: accelerator at 0x%08x (size 0x%x), irq pl2ps_irq[%d] = GIC ID %d / SPI %d\n",
               (unsigned)DOT_BASE_ADDR, (unsigned)DOT_WINDOW_SIZE, DOT_PL2PS_IRQ,
               DOT_GIC_HWIRQ, DOT_GIC_SPI);

        SC_THREAD(pull_reset);
    }

private:
    tlm_utils::tlm_quantumkeeper m_qk;
};

static void usage(void)
{
    cout << "usage: zynqmp_dot <socket-path> <sync-quantum-ns> [-v <0..3>]" << endl;
}

int sc_main(int argc, char *argv[])
{
    uint64_t sync_quantum;

    if (argc < 3) {
        usage();
        exit(EXIT_FAILURE);
    }
    sync_quantum = strtoull(argv[2], NULL, 10);

    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-v") && i + 1 < argc)
            dot_log_level() = atoi(argv[++i]);
        else {
            usage();
            exit(EXIT_FAILURE);
        }
    }

    sc_set_time_resolution(1, SC_PS);

    new Top("top", argv[1], sc_time((double) sync_quantum, SC_NS));

    sc_start();
    return 0;
}
