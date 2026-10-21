/* The MC68331's on-chip modules, as their registers lie and as their
 * bits are named, from the MC68331 User's Manual
 * (third_party/motorola/MC68331UM.pdf), Appendix D.  The modules sit at
 * $YFF000 with Y = M111, and the SIM's MM bit set maps them at 0xfff000
 * in these images.  Each structure is one module's registers from its
 * base, the manual's "not used" bytes reserved; the bits are #defines,
 * which SmartClockSetup.java leaves out of what Ghidra parses (its C
 * parser takes no preprocessor lines) and SmartClockExportC.java copies
 * into the C.  Parsed after smartclock.h, whose integer types it uses. */

#pragma pack(1)

/* GPT, the general-purpose timer, at 0xfff900: Table D-2. */
typedef struct gpt_registers {
	uint16_t gptmcr;      /* +0x00: D.2.1 */
	uint16_t gptmtr;      /* +0x02: reserved for factory test */
	uint16_t icr;         /* +0x04: D.2.3 */
	uint8_t ddrgp;        /* +0x06 */
	uint8_t portgp;       /* +0x07 */
	uint8_t oc1m;         /* +0x08 */
	uint8_t oc1d;         /* +0x09 */
	uint16_t tcnt;        /* +0x0a: the free-running counter */
	uint8_t pactl;        /* +0x0c */
	uint8_t pacnt;        /* +0x0d */
	uint16_t tic[3];      /* +0x0e: input captures 1 to 3 */
	uint16_t toc[4];      /* +0x14: output compares 1 to 4 */
	uint16_t ti4_o5;      /* +0x1c */
	uint8_t tctl1;        /* +0x1e */
	uint8_t tctl2;        /* +0x1f */
	uint8_t tmsk1;        /* +0x20: D.2.12 */
	uint8_t tmsk2;        /* +0x21 */
	uint8_t tflg1;        /* +0x22: D.2.13 */
	uint8_t tflg2;        /* +0x23 */
	uint8_t cforc;        /* +0x24: D.2.14 */
	uint8_t pwmc;         /* +0x25: D.2.14 */
	uint8_t pwma;         /* +0x26 */
	uint8_t pwmb;         /* +0x27 */
	uint16_t pwmcnt;      /* +0x28 */
	uint8_t pwmbufa;      /* +0x2a */
	uint8_t pwmbufb;      /* +0x2b */
	uint16_t prescl;      /* +0x2c */
} gpt_registers;

/* A chip select's base and option registers. */
typedef struct sim_chip_select {
	uint16_t csbar;
	uint16_t csor;
} sim_chip_select;

/* SIM, the system integration module, at 0xfffa00: Table D-3. */
typedef struct sim_registers {
	uint16_t simcr;       /* +0x00: D.3.1 */
	uint16_t simtr;       /* +0x02: factory test */
	uint16_t syncr;       /* +0x04: the clock synthesizer */
	uint8_t reserved_06;
	uint8_t rsr;          /* +0x07: why the last reset happened */
	uint16_t simtre;      /* +0x08 */
	uint8_t reserved_0a[6];
	uint8_t reserved_10;
	uint8_t porte0;       /* +0x11 */
	uint8_t reserved_12;
	uint8_t porte1;       /* +0x13 */
	uint8_t reserved_14;
	uint8_t ddre;         /* +0x15 */
	uint8_t reserved_16;
	uint8_t pepar;        /* +0x17 */
	uint8_t reserved_18;
	uint8_t portf0;       /* +0x19 */
	uint8_t reserved_1a;
	uint8_t portf1;       /* +0x1b */
	uint8_t reserved_1c;
	uint8_t ddrf;         /* +0x1d */
	uint8_t reserved_1e;
	uint8_t pfpar;        /* +0x1f */
	uint8_t reserved_20;
	uint8_t sypcr;        /* +0x21: D.3.12, the watchdog and bus monitors */
	uint16_t picr;        /* +0x22: D.3.13 */
	uint16_t pitr;        /* +0x24: D.3.14 */
	uint8_t reserved_26;
	uint8_t swsr;         /* +0x27: the watchdog's service register */
	uint8_t reserved_28[8];
	uint16_t tstmsra;     /* +0x30: the test module's */
	uint16_t tstmsrb;
	uint16_t tstsc;
	uint16_t tstrc;
	uint16_t creg;
	uint16_t dreg;
	uint8_t reserved_3c[4];
	uint8_t reserved_40;
	uint8_t portc;        /* +0x41 */
	uint8_t reserved_42[2];
	uint16_t cspar0;      /* +0x44 */
	uint16_t cspar1;      /* +0x46 */
	sim_chip_select csboot; /* +0x48: CSBARBT and CSORBT */
	sim_chip_select cs[11]; /* +0x4c: CSBAR0 and CSOR0 to CSBAR10 and CSOR10 */
} sim_registers;

/* QSM, the queued serial module, at 0xfffc00: Table D-13.  Its SCI is
 * the serial port sci_init drives, its QSPI the bus to the ADC, the DACs
 * and the FPGA. */
typedef struct qsm_registers {
	uint16_t qsmcr;       /* +0x00: D.4.1 */
	uint16_t qtest;       /* +0x02 */
	uint8_t qilr;         /* +0x04: D.4.3 */
	uint8_t qivr;         /* +0x05 */
	uint16_t reserved_06;
	uint16_t sccr0;       /* +0x08: D.4.4, the baud divisor */
	uint16_t sccr1;       /* +0x0a: D.4.5 */
	uint16_t scsr;        /* +0x0c: D.4.6 */
	uint16_t scdr;        /* +0x0e: D.4.7 */
	uint8_t reserved_10[4];
	uint8_t reserved_14;
	uint8_t portqs;       /* +0x15 */
	uint8_t pqspar;       /* +0x16 */
	uint8_t ddrqs;        /* +0x17 */
	uint16_t spcr0;       /* +0x18: D.4.10 */
	uint16_t spcr1;       /* +0x1a: D.4.11 */
	uint16_t spcr2;       /* +0x1c: D.4.12 */
	uint8_t spcr3;        /* +0x1e: D.4.13 */
	uint8_t spsr;         /* +0x1f: D.4.13 */
	uint8_t reserved_20[0xe0];
	uint16_t rr[16];      /* +0x100: the receive RAM */
	uint16_t tr[16];      /* +0x120: the transmit RAM */
	uint8_t cr[16];       /* +0x140: the command RAM */
} qsm_registers;

/* QSMCR, D.4.1 */
#define QSMCR_STOP     0x8000
#define QSMCR_FRZ1     0x4000
#define QSMCR_FRZ0     0x2000
#define QSMCR_SUPV     0x0080
#define QSMCR_IARB_MASK 0x000f

/* SCCR1, D.4.5 */
#define SCCR1_LOOPS    0x4000
#define SCCR1_WOMS     0x2000
#define SCCR1_ILT      0x1000
#define SCCR1_PT       0x0800  /* odd parity, else even */
#define SCCR1_PE       0x0400  /* parity enabled */
#define SCCR1_M        0x0200  /* an 11-bit frame, else 10 */
#define SCCR1_WAKE     0x0100
#define SCCR1_TIE      0x0080  /* the transmit-data-register-empty interrupt */
#define SCCR1_TCIE     0x0040
#define SCCR1_RIE      0x0020  /* the receive and overrun interrupt */
#define SCCR1_ILIE     0x0010
#define SCCR1_TE       0x0008
#define SCCR1_RE       0x0004
#define SCCR1_RWU      0x0002
#define SCCR1_SBK      0x0001

/* SCSR, D.4.6 */
#define SCSR_TDRE      0x0100
#define SCSR_TC        0x0080
#define SCSR_RDRF      0x0040
#define SCSR_RAF       0x0020
#define SCSR_IDLE      0x0010
#define SCSR_OR        0x0008  /* overrun */
#define SCSR_NF        0x0004  /* noise */
#define SCSR_FE        0x0002  /* framing error or break */
#define SCSR_PF        0x0001  /* parity error */

/* SPCR0, D.4.10 */
#define SPCR0_MSTR     0x8000
#define SPCR0_WOMQ     0x4000
#define SPCR0_BITS_MASK 0x3c00
#define SPCR0_BITS_SHIFT 10
#define SPCR0_CPOL     0x0200
#define SPCR0_CPHA     0x0100
#define SPCR0_SPBR_MASK 0x00ff

/* SPCR1, D.4.11 */
#define SPCR1_SPE      0x8000  /* the QSPI runs its queue */
#define SPCR1_DSCKL_MASK 0x7f00
#define SPCR1_DSCKL_SHIFT 8
#define SPCR1_DTL_MASK 0x00ff

/* SPCR2, D.4.12 */
#define SPCR2_SPIFIE   0x8000  /* the end-of-queue interrupt */
#define SPCR2_WREN     0x4000
#define SPCR2_WRTO     0x2000
#define SPCR2_ENDQP_MASK 0x0f00
#define SPCR2_ENDQP_SHIFT 8
#define SPCR2_NEWQP_MASK 0x000f

/* SPCR3 and SPSR, D.4.13: SPCR3 the high byte at 0xfffc1e, SPSR the low */
#define SPCR3_LOOPQ    0x04
#define SPCR3_HMIE     0x02
#define SPCR3_HALT     0x01
#define SPSR_SPIF      0x80  /* the queue is done */
#define SPSR_MODF      0x40
#define SPSR_HALTA     0x20
#define SPSR_CPTQP_MASK 0x0f

/* A command RAM entry, D.4.16 */
#define QSPI_CR_CONT   0x80
#define QSPI_CR_BITSE  0x40
#define QSPI_CR_DT     0x20
#define QSPI_CR_DSCK   0x10
#define QSPI_CR_PCS_MASK 0x0f

/* SIMCR, D.3.1 */
#define SIMCR_EXOFF    0x8000
#define SIMCR_FRZSW    0x4000
#define SIMCR_FRZBM    0x2000
#define SIMCR_SLVEN    0x0800
#define SIMCR_SHEN_MASK 0x0300
#define SIMCR_SUPV     0x0080
#define SIMCR_MM       0x0040  /* the modules at 0xfff000, else 0x7ff000 */
#define SIMCR_IARB_MASK 0x000f

/* SYPCR, D.3.12 */
#define SYPCR_SWE      0x80  /* the software watchdog enabled */
#define SYPCR_SWP      0x40
#define SYPCR_SWT_MASK 0x30
#define SYPCR_HME      0x08
#define SYPCR_BME      0x04
#define SYPCR_BMT_MASK 0x03

/* PICR, D.3.13 */
#define PICR_PIRQL_MASK 0x0700
#define PICR_PIRQL_SHIFT 8
#define PICR_PIV_MASK  0x00ff

/* PITR, D.3.14 */
#define PITR_PTP       0x0100
#define PITR_PITM_MASK 0x00ff

/* TMSK1 and TMSK2, D.2.12: TMSK1 the high byte, TMSK2 the low */
#define TMSK1_I4O5I    0x80
#define TMSK1_OCI_MASK 0x78
#define TMSK1_ICI_MASK 0x07
#define TMSK2_TOI      0x80
#define TMSK2_PAOVI    0x20
#define TMSK2_PAII     0x10
#define TMSK2_CPROUT   0x08
#define TMSK2_CPR_MASK 0x07

/* TFLG1 and TFLG2, D.2.13 */
#define TFLG1_I4O5F    0x80
#define TFLG1_OCF_MASK 0x78
#define TFLG1_ICF_MASK 0x07
#define TFLG2_TOF      0x80
#define TFLG2_PAOVF    0x20
#define TFLG2_PAIF     0x10

/* CFORC and PWMC, D.2.14: CFORC the high byte, PWMC the low */
#define CFORC_FOC_MASK 0xf8
#define CFORC_FPWMA    0x02  /* PWMA forced to F1A's level */
#define CFORC_FPWMB    0x01
#define PWMC_PPROUT    0x80
#define PWMC_PPR_MASK  0x70
#define PWMC_SFA       0x08
#define PWMC_SFB       0x04
#define PWMC_F1A       0x02
#define PWMC_F1B       0x01
