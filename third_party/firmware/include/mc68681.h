/* The 68681 DUART on chip select 8, as its registers lie and as their
 * bits are named, from Motorola's MC68681 data sheet
 * (third_party/motorola/MC68681.pdf): Table 4-1 for the registers'
 * order, Table 4-5 for their bits.  The firmware programs the IVR, which
 * a 2681 lacks, so the part is a 68681; which maker's is not confirmed
 * from a board.  It sits on an 8-bit port, register n at 0x200000 +
 * 2n + 1.  A register read and the one written at the same address are
 * one field, named read then write.  The bits are #defines, as
 * mc68331.h's are.  Parsed after smartclock.h. */

#pragma pack(1)

/* A channel's four registers, with the even bytes between them. */
typedef struct duart_channel {
  uint8_t reserved_0;
  uint8_t mr; /* +0x01: MR1 then MR2, the pointer moving on after MR1 */
  uint8_t reserved_2;
  uint8_t
      sr_csr; /* +0x03: the status register read, the clock-select written */
  uint8_t reserved_4;
  uint8_t cr; /* +0x05: the command register, written only */
  uint8_t reserved_6;
  uint8_t rb_tb; /* +0x07: the receiver buffer read, the transmitter buffer
                    written */
} duart_channel;

/* The DUART, at 0x200000: Table 4-1. */
typedef struct duart_registers {
  duart_channel a; /* +0x00: registers 0 to 3 */
  uint8_t reserved_08;
  uint8_t ipcr_acr; /* +0x09: the input-port change register read, the auxiliary
                       control written */
  uint8_t reserved_0a;
  uint8_t isr_imr; /* +0x0b: the interrupt status read, the interrupt mask
                      written */
  uint8_t reserved_0c;
  uint8_t
      cur_ctur; /* +0x0d: the counter's current value, its preload written */
  uint8_t reserved_0e;
  uint8_t clr_ctlr; /* +0x0f */
  duart_channel b;  /* +0x10: registers 8 to 11 */
  uint8_t reserved_18;
  uint8_t ivr; /* +0x19: the interrupt vector, read and written */
  uint8_t reserved_1a;
  uint8_t ip_opcr; /* +0x1b: the input port read, the output-port configuration
                      written */
  uint8_t reserved_1c;
  uint8_t start_sopbc; /* +0x1d: reading starts the counter; writing sets
                          output-port bits */
  uint8_t reserved_1e;
  uint8_t
      stop_ropbc; /* +0x1f: reading stops it; writing resets output-port bits */
} duart_registers;

/* MR1, Table 4-5 sheet 1 */
#define MR1_RXRTS 0x80 /* the receiver negates RTS when the FIFO is full */
/* the receive interrupt on FIFO full, else on RxRDY */
#define MR1_RXIRQ_FFULL 0x40
/* the error bits accumulate, else per character */
#define MR1_ERROR_BLOCK 0x20
#define MR1_PARITY_MASK 0x18
#define MR1_PARITY_WITH 0x00
#define MR1_PARITY_FORCE 0x08
#define MR1_PARITY_NONE 0x10
#define MR1_MULTIDROP 0x18
#define MR1_PARITY_ODD 0x04 /* with parity, odd; forced, high */
#define MR1_BITS_MASK 0x03  /* 5 to 8 bits, 0 to 3 */

/* MR2, Table 4-5 sheet 1 */
#define MR2_MODE_MASK 0xc0
#define MR2_MODE_NORMAL 0x00
#define MR2_MODE_ECHO 0x40
#define MR2_MODE_LOCAL_LOOPBACK 0x80
#define MR2_MODE_REMOTE_LOOPBACK 0xc0
#define MR2_TXRTS 0x20
#define MR2_CTS 0x10       /* the transmitter waits for CTS */
#define MR2_STOP_MASK 0x0f /* 7 is one stop bit, 0xf two */

/* CR, Table 4-5 sheet 3: a miscellaneous command in bits 6:4, with the
 * transmitter's in bits 3:2 and the receiver's in bits 1:0 */
#define CR_RESET_MR_POINTER 0x10
#define CR_RESET_RECEIVER 0x20
#define CR_RESET_TRANSMITTER 0x30
#define CR_RESET_ERROR 0x40
#define CR_RESET_BREAK_CHANGE 0x50
#define CR_START_BREAK 0x60
#define CR_STOP_BREAK 0x70
#define CR_TX_ENABLE 0x04
#define CR_TX_DISABLE 0x08
#define CR_RX_ENABLE 0x01
#define CR_RX_DISABLE 0x02

/* SR, Table 4-5 sheet 3 */
#define SR_RECEIVED_BREAK 0x80
#define SR_FRAMING_ERROR 0x40
#define SR_PARITY_ERROR 0x20
#define SR_OVERRUN 0x10
#define SR_TXEMT 0x08
#define SR_TXRDY 0x04
#define SR_FFULL 0x02
#define SR_RXRDY 0x01

/* ACR, Table 4-5 sheet 4 */
#define ACR_BRG_SET_2 0x80 /* baud-rate set 2, else set 1 */
#define ACR_COUNTER_MASK 0x70
#define ACR_IP3_IRQ 0x08
#define ACR_IP2_IRQ 0x04
#define ACR_IP1_IRQ 0x02
#define ACR_IP0_IRQ 0x01

/* ISR and IMR, Table 4-5 sheet 4: the same bits, the IMR's passing the
 * ISR's to the interrupt */
#define ISR_INPUT_CHANGE 0x80
#define ISR_BREAK_B 0x40
#define ISR_RXRDYB 0x20
#define ISR_TXRDYB 0x10
#define ISR_COUNTER 0x08
#define ISR_BREAK_A 0x04
#define ISR_RXRDYA 0x02
#define ISR_TXRDYA 0x01
#define IMR_INPUT_CHANGE 0x80
#define IMR_BREAK_B 0x40
#define IMR_RXRDYB 0x20
#define IMR_TXRDYB 0x10
#define IMR_COUNTER 0x08
#define IMR_BREAK_A 0x04
#define IMR_RXRDYA 0x02
#define IMR_TXRDYA 0x01

/* OPCR, Table 4-5 sheet 3: each bit 0 for the output port register's */
#define OPCR_OP7_TXRDYB 0x80
#define OPCR_OP6_TXRDYA 0x40
#define OPCR_OP5_RXRDYB 0x20
#define OPCR_OP4_RXRDYA 0x10
#define OPCR_OP3_MASK 0x0c
#define OPCR_OP2_MASK 0x03

/* The output port, 4.2.12: the register holds the complement of each
 * pin's level, so setting a bit (SOPBC) drives the pin low */
