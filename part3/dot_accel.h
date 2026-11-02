/*
 * dot_accel.h: SystemC / TLM-2.0 model of the dot product accelerator
 */
#ifndef DOT_ACCEL_H
#define DOT_ACCEL_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include <stdio.h>
#include <string.h>

#include "dot_q8_0_core.h"
#include "dot_accel_regs.h"

inline int &dot_log_level() { static int lvl = 1; return lvl; }

#define DOT_LOG(lvl, ...)                                                    \
    do {                                                                     \
        if (dot_log_level() >= (lvl)) {                                      \
            printf("[%12s] DotAccel: ",                                      \
                   sc_core::sc_time_stamp().to_string().c_str());            \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)


#define DOT_STATIC_ASSERT(name, cond) typedef char dot_assert_##name[(cond) ? 1 : -1]
DOT_STATIC_ASSERT(xq_size,     DOT_XQ_BYTES == DOT_MAX_N);
DOT_STATIC_ASSERT(xd_size,     DOT_XD_BYTES == DOT_MAX_NB * sizeof(dot_half_t));
DOT_STATIC_ASSERT(spad_fit,    DOT_SPAD_YD + DOT_XD_BYTES <= DOT_WINDOW_SIZE);
DOT_STATIC_ASSERT(mem_res,     DOT_SPAD_YD + DOT_XD_BYTES <= DOT_MEM_OFFSET + DOT_MEM_SIZE);
/*
 * Timing model
 *   cycles(nb) = startup_cycles + nb * block_cycles + finish_cycles
 */

struct DotTiming {
    const char *name;
    double   clk_ns;            
    unsigned startup_cycles;    
    unsigned block_cycles;      
    unsigned finish_cycles;     
    unsigned reg_cycles;       
    unsigned spad_word_cycles;  

    unsigned total_cycles(int nb) const
    {
        return startup_cycles + (unsigned)nb * block_cycles + finish_cycles;
    }
    sc_core::sc_time period() const { return sc_core::sc_time(clk_ns, sc_core::SC_NS); }

    /* Lab 2 design_7: pipelined BLOCK_LOOP, II=4, iteration latency 21 */
    static DotTiming design7()
    {
        DotTiming t = { "design_7 (II=4)", 10.0, 1, 4, 17, 1, 1 };
        return t;
    }
};


SC_MODULE(DotAccel)
{
    
    tlm_utils::simple_target_socket<DotAccel> socket;
    sc_out<bool> irq;

    DotTiming timing;   

    SC_HAS_PROCESS(DotAccel);
    DotAccel(sc_module_name name, const DotTiming &t = DotTiming::design7())
        : sc_module(name), socket("socket"), irq("irq"), timing(t),
          n_calls(0), busy_time(SC_ZERO_TIME),
          busy_flag(false), done_flag(false), err_flag(false), irq_en(false),
          reg_nb(0), result_val(0.0f), last_cycles(0), cur_block(-1)
    {
        memset(spad_xq, 0, sizeof spad_xq);
        memset(spad_yq, 0, sizeof spad_yq);
        memset(spad_xd, 0, sizeof spad_xd);
        memset(spad_yd, 0, sizeof spad_yd);

        socket.register_b_transport(this, &DotAccel::b_transport);

        SC_THREAD(compute_process);
        SC_METHOD(irq_process);          
        sensitive << irq_ev;
    }

    void trace(sc_trace_file *tf)
    {
        sc_trace(tf, busy_flag,  "dot_busy");
        sc_trace(tf, done_flag,  "dot_done");
        sc_trace(tf, err_flag,   "dot_err");
        sc_trace(tf, reg_nb,     "dot_nb");
        sc_trace(tf, cur_block,  "dot_cur_block");
        sc_trace(tf, result_val, "dot_result");
        sc_trace(tf, irq,        "dot_irq");
    }

    void end_of_simulation()
    {
        printf("[%12s] DotAccel: %u call(s), total busy time %s\n",
               sc_time_stamp().to_string().c_str(), n_calls, busy_time.to_string().c_str());
    }

    unsigned n_calls;    
    sc_time  busy_time; 

private:
    
    bool     busy_flag, done_flag, err_flag, irq_en;
    int32_t  reg_nb;
    float    result_val;
    uint32_t last_cycles;
    int      cur_block;       

    sc_event start_ev;       
    sc_event irq_ev;         

    int8_t     spad_xq[DOT_MAX_N];
    int8_t     spad_yq[DOT_MAX_N];
    dot_half_t spad_xd[DOT_MAX_NB];
    dot_half_t spad_yd[DOT_MAX_NB];

    /* TLM target side */

    void b_transport(tlm::tlm_generic_payload &tr, sc_time &delay)
    {
        tlm::tlm_command cmd = tr.get_command();
        sc_dt::uint64 addr   = tr.get_address();
        unsigned len         = tr.get_data_length();

        tr.set_dmi_allowed(false);          

        if (cmd == tlm::TLM_IGNORE_COMMAND) {
            tr.set_response_status(tlm::TLM_OK_RESPONSE);
            return;
        }
        bool write = (cmd == tlm::TLM_WRITE_COMMAND);

        tlm::tlm_response_status st;
        unsigned words = (len + 3) / 4;
        if (addr < DOT_REG_WINDOW) {
            delay += words * timing.reg_cycles * timing.period();
            st = access_regs(write, (unsigned)addr, tr.get_data_ptr(), len, delay);
        } else {
            delay += words * timing.spad_word_cycles * timing.period();
            st = access_spad(write, (unsigned)addr, tr.get_data_ptr(), len);
        }
        tr.set_response_status(st);
    }

    static const char *reg_name(unsigned off)
    {
        switch (off) {
        case DOT_REG_CTRL:   return "CTRL";
        case DOT_REG_STATUS: return "STATUS";
        case DOT_REG_NB:     return "NB";
        case DOT_REG_RESULT: return "RESULT";
        case DOT_REG_IRQ_EN: return "IRQ_EN";
        case DOT_REG_CYCLES: return "CYCLES";
        case DOT_REG_CALLS:  return "CALLS";
        case DOT_REG_ID:     return "ID";
        default:             return "?";
        }
    }

    uint32_t status_word() const
    {
        return (busy_flag ? DOT_STATUS_BUSY : 0) | (done_flag ? DOT_STATUS_DONE : 0) |
               (err_flag  ? DOT_STATUS_ERR  : 0);
    }

    tlm::tlm_response_status access_regs(bool write, unsigned addr, unsigned char *data,
                                         unsigned len, const sc_time &delay)
    {
        if ((addr % 4) || (len % 4) || len == 0) {
            DOT_LOG(1, "ERROR: unaligned/odd-sized register access addr=0x%x len=%u", addr, len);
            return tlm::TLM_BURST_ERROR_RESPONSE;
        }
        if (addr + len > DOT_REG_WINDOW)
            return tlm::TLM_ADDRESS_ERROR_RESPONSE;

        for (unsigned i = 0; i < len; i += 4) {
            unsigned off = addr + i;
            uint32_t v = 0;
            if (write) {
                memcpy(&v, data + i, 4);
                if (!write_reg(off, v, delay))
                    return tlm::TLM_ADDRESS_ERROR_RESPONSE;
            } else {
                if (!read_reg(off, &v))
                    return tlm::TLM_ADDRESS_ERROR_RESPONSE;
                memcpy(data + i, &v, 4);
            }
            DOT_LOG(2, "%s %-6s (0x%04x) = 0x%08x", write ? "WR" : "RD", reg_name(off), off, v);
        }
        return tlm::TLM_OK_RESPONSE;
    }

    bool read_reg(unsigned off, uint32_t *v)
    {
        switch (off) {
        case DOT_REG_CTRL:   *v = 0;                        return true;
        case DOT_REG_STATUS: *v = status_word();            return true;
        case DOT_REG_NB:     *v = (uint32_t)reg_nb;         return true;
        case DOT_REG_RESULT: memcpy(v, &result_val, 4);     return true;
        case DOT_REG_IRQ_EN: *v = irq_en ? 1 : 0;           return true;
        case DOT_REG_CYCLES: *v = last_cycles;              return true;
        case DOT_REG_CALLS:  *v = n_calls;                  return true;
        case DOT_REG_ID:     *v = DOT_ID_VALUE;             return true;
        default:             *v = 0;                        return false;
        }
    }

    bool write_reg(unsigned off, uint32_t v, const sc_time &delay)
    {
        switch (off) {
        case DOT_REG_CTRL:
            if (v & DOT_CTRL_CLEAR) {               
                if (!busy_flag) done_flag = false;
                err_flag = false;
                irq_ev.notify();
            }
            if (v & DOT_CTRL_START) {
                if (busy_flag) {                    
                    DOT_LOG(1, "WARNING: START while BUSY ignored (ERR set)");
                    err_flag = true;
                } else {
                    busy_flag = true;               
                    done_flag = false;
                    irq_ev.notify();
                    start_ev.notify(delay);         
                }
            }
            return true;
        case DOT_REG_NB:
            if (busy_flag) {
                DOT_LOG(1, "WARNING: write to NB while BUSY ignored (ERR set)");
                err_flag = true;
            } else {
                reg_nb = (int32_t)v;
            }
            return true;
        case DOT_REG_IRQ_EN:
            irq_en = (v & 1) != 0;
            irq_ev.notify();
            return true;
        case DOT_REG_STATUS: case DOT_REG_RESULT: case DOT_REG_CYCLES:
        case DOT_REG_CALLS:  case DOT_REG_ID:
            DOT_LOG(1, "WARNING: write to read-only register %s ignored", reg_name(off));
            return true;                            /* not a bus error: just ignored */
        default:
            return false;
        }
    }

    tlm::tlm_response_status access_spad(bool write, unsigned addr, unsigned char *data, unsigned len)
    {
        struct Region { unsigned base, bytes; unsigned char *mem; const char *name; };
        Region regions[4] = {
            { DOT_SPAD_XQ, DOT_XQ_BYTES, (unsigned char *)spad_xq, "XQ" },
            { DOT_SPAD_YQ, DOT_XQ_BYTES, (unsigned char *)spad_yq, "YQ" },
            { DOT_SPAD_XD, DOT_XD_BYTES, (unsigned char *)spad_xd, "XD" },
            { DOT_SPAD_YD, DOT_XD_BYTES, (unsigned char *)spad_yd, "YD" },
        };
        for (int r = 0; r < 4; r++) {
            if (addr >= regions[r].base && addr < regions[r].base + regions[r].bytes) {
                unsigned off = addr - regions[r].base;
                if (off + len > regions[r].bytes) {
                    DOT_LOG(1, "ERROR: %s access beyond end of array: off=%u len=%u size=%u",
                            regions[r].name, off, len, regions[r].bytes);
                    return tlm::TLM_ADDRESS_ERROR_RESPONSE;
                }
                if (write) {
                    if (busy_flag) {            
                        DOT_LOG(1, "WARNING: write to %s while BUSY dropped (ERR set)", regions[r].name);
                        err_flag = true;
                        return tlm::TLM_OK_RESPONSE;
                    }
                    memcpy(regions[r].mem + off, data, len);
                    DOT_LOG(3, "WR %s[%u..%u)", regions[r].name, off, off + len);
                } else {
                    memcpy(data, regions[r].mem + off, len);
                    DOT_LOG(3, "RD %s[%u..%u)", regions[r].name, off, off + len);
                }
                return tlm::TLM_OK_RESPONSE;
            }
        }
        DOT_LOG(1, "ERROR: access to unmapped address 0x%x", addr);
        return tlm::TLM_ADDRESS_ERROR_RESPONSE;
    }

    void irq_process()
    {
        bool level = irq_en && done_flag;
        if (irq.read() != level)
            DOT_LOG(2, "irq -> %d", level ? 1 : 0);
        irq.write(level);
    }

    void compute_process()
    {
        for (;;) {
            wait(start_ev);                            

            sc_time t_begin = sc_time_stamp();
            int nb = reg_nb;
            if (nb > DOT_MAX_NB) {                     
                DOT_LOG(1, "nb=%d clamped to %d", nb, DOT_MAX_NB);
                nb = DOT_MAX_NB;
            }
            if (nb < 0) nb = 0;

            DOT_LOG(1, "START nb=%d (model: %u cycles = %s, %s)", nb, timing.total_cycles(nb),
                    (timing.total_cycles(nb) * timing.period()).to_string().c_str(), timing.name);

            wait(timing.startup_cycles * timing.period());

            float sumf = 0.0f;
            for (int ib = 0; ib < nb; ib++) {         
                cur_block = ib;
                float term = dot_q8_0_block(&spad_xq[ib * DOT_QK], &spad_yq[ib * DOT_QK],
                                            spad_xd[ib], spad_yd[ib]);
                sumf += term;                          
                DOT_LOG(3, "block %3d: term=%.9g sumf=%.9g", ib, term, sumf);

                wait(timing.block_cycles * timing.period());
                DOT_LOG(2, "block %3d done", ib);
            }

            wait(timing.finish_cycles * timing.period());

            sc_time dt = sc_time_stamp() - t_begin;
            result_val  = sumf;
            last_cycles = (uint32_t)(dt / timing.period() + 0.5);
            cur_block   = -1;
            n_calls++;
            busy_time  += dt;
            busy_flag   = false;
            done_flag   = true;                        
            irq_ev.notify();
            DOT_LOG(1, "DONE  result=%.9g after %s (%u cycles)", sumf,
                    dt.to_string().c_str(), last_cycles);
        }
    }
};

#endif
