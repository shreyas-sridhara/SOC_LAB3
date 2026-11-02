/*
 * dot_accel_regs.h  --  register / memory map of the dot product accelerator
 *
 *
 *  Offset    Name     Access  Description
 *
 *  0x0000    CTRL     W       bit0 START : launch one dot product 
 *                             bit1 CLEAR : clear DONE, ERR and the interrupt
 *                             
 *  0x0004    STATUS   R       bit0 BUSY  : computing
 *                             bit1 DONE  : result valid 
 *                             bit2 ERR   : Error (START while busy,
 *                                          scratchpad/NB write while busy)
 *  0x0008    NB       R/W     number of 32-element blocks (clamped to 152 for our model in hw)
 *  0x000C    RESULT   R       FP32 result
 *  0x0010    IRQ_EN   R/W     bit0: raise irq line while DONE is set
 *  0x0014    CYCLES   R       accelerator clock cycles of the last dot product
 *  0x0018    CALLS    R       number of completed dot products
 *  0x001C    ID       R       DOT_ID_VALUE (check the device)
 *
 *  0x10000   XQ       R/W     x quants,  4864 x int8
 *  0x12000   YQ       R/W     y quants,  4864 x int8
 *  0x14000   XD       R/W     x scales,  152  x FP16 
 *  0x15000   YD       R/W     y scales,  152  x FP16
 *
 * Typical sequence:
 *   1. write XQ,YQ,XD,YD  2. write NB  3. write CTRL=START
 *   4. poll STATUS until DONE   5. read RESULT   6. write CTRL=CLEAR
 * With IRQ_EN=1 step 4 becomes "sleep until the interrupt".
 */
#ifndef DOT_ACCEL_REGS_H
#define DOT_ACCEL_REGS_H

#define DOT_REG_CTRL      0x0000
#define DOT_REG_STATUS    0x0004
#define DOT_REG_NB        0x0008
#define DOT_REG_RESULT    0x000C
#define DOT_REG_IRQ_EN    0x0010
#define DOT_REG_CYCLES    0x0014
#define DOT_REG_CALLS     0x0018
#define DOT_REG_ID        0x001C
#define DOT_REG_WINDOW    0x0100          

#define DOT_CTRL_START    (1u << 0)
#define DOT_CTRL_CLEAR    (1u << 1)

#define DOT_STATUS_BUSY   (1u << 0)
#define DOT_STATUS_DONE   (1u << 1)
#define DOT_STATUS_ERR    (1u << 2)

#define DOT_ID_VALUE      0xD07A0001u

#define DOT_SPAD_XQ       0x10000
#define DOT_SPAD_YQ       0x12000
#define DOT_SPAD_XD       0x14000
#define DOT_SPAD_YD       0x15000

#define DOT_XQ_BYTES      4864         
#define DOT_XD_BYTES      304             

#define DOT_WINDOW_SIZE   0x20000         

/*
 *
 * Two memory resources + one interrupt:
 *   Regs: DOT_BASE_ADDR + 0                size DOT_REG_WINDOW
 *   Mem : DOT_BASE_ADDR + DOT_MEM_OFFSET   size DOT_MEM_SIZE
 * Offsets (scratchpad offset - DOT_MEM_OFFSET)
 */
#define DOT_BASE_ADDR     0xa0100000u
#define DOT_MEM_OFFSET    0x10000
#define DOT_MEM_SIZE      0x6000          
#define DOT_MEM_XQ        (DOT_SPAD_XQ - DOT_MEM_OFFSET)   /* 0x0000 */
#define DOT_MEM_YQ        (DOT_SPAD_YQ - DOT_MEM_OFFSET)   /* 0x2000 */
#define DOT_MEM_XD        (DOT_SPAD_XD - DOT_MEM_OFFSET)   /* 0x4000 */
#define DOT_MEM_YD        (DOT_SPAD_YD - DOT_MEM_OFFSET)   /* 0x5000 */

/* SystemC side: dot.irq is bound to zynq.pl2ps_irq[DOT_PL2PS_IRQ].
 * SPI 0x59 = 89 = GIC ID 121, matching GIC ID = 121 + n / SPI = 89 + n.
 */
#define DOT_PL2PS_IRQ     5
#define DOT_GIC_HWIRQ     (121 + DOT_PL2PS_IRQ)   /* 126 */
#define DOT_GIC_SPI       (89 + DOT_PL2PS_IRQ)    /* 94  */

#endif
