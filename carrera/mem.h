/*
 * Memory and machine-specific definitions.  Used in C and assembler.
 */

/*
 * Sizes
 */

#define	BI2BY		8			/* bits per byte */
#define BI2WD		32			/* bits per word */
#define	BY2WD		4			/* bytes per word */
#define	BY2PG		4096			/* bytes per page */
#define	WD2PG		(BY2PG/BY2WD)		/* words per page */
#define	PGSHIFT		12			/* log(BY2PG) */
#define PGROUND(s)	(((s)+(BY2PG-1))&~(BY2PG-1))

#define	MAXMACH		4			/* max # cpus system can run */
#define KSTACK		4096			/* Size of kernel stack */

/*
 * Time
 */
#define HZ		100			/* clock frequency */
#define	MS2HZ		(1000/HZ)		/* millisec per clock tick */
#define	TK2SEC(t)	((t)/HZ)		/* ticks to seconds */
#define	TK2MS(t)	((t)*MS2HZ)		/* ticks to milliseconds */
#define	MS2TK(t)	((t)/MS2HZ)		/* milliseconds to ticks */

/*
 * CP0 registers
 */

#define INDEX		0
#define RANDOM		1
#define TLBPHYS0	2
#define TLBPHYS1	3
#define CONTEXT		4
#define PAGEMASK	5
#define WIRED		6
#define BADVADDR	8
#define COUNT		9
#define TLBVIRT		10
#define COMPARE		11
#define STATUS		12
#define CAUSE		13
#define EPC		14
#define	PRID		15
#define	CONFIG		16
#define	LLADDR		17
#define	WATCHLO		18
#define	WATCHHI		19
#define	CACHEECC	26
#define	CACHEERR	27
#define	TAGLO		28
#define	TAGHI		29
#define	ERROREPC	30

/*
 * M(STATUS) bits
 */
#define KMODEMASK	0x0000001f
#define IE		0x00000001
#define EXL		0x00000002
#define ERL		0x00000004
#define KSUPER		0x00000008
#define KUSER		0x00000010
#define INTMASK		0x0000ff00
#define INTR0		0x00000100
#define INTR1		0x00000200
#define INTR2		0x00000400
#define INTR3		0x00000800
#define INTR4		0x00001000
#define INTR5		0x00002000
#define INTR6		0x00004000
#define INTR7		0x00008000
#define ISC		0x00010000
#define SWC		0x00020000
#define CU1		0x20000000

/*
 * Trap vectors
 */
#define	UTLBMISS	(KSEG0+0x000)
#define	XEXCEPTION	(KSEG0+0x80)
#define	CACHETRAP	(KSEG0+0x100)
#define	EXCEPTION	(KSEG0+0x180)

/*
 * Magic registers
 */
#define	USER		24		/* R24 is up-> */
#define	MACH		25		/* R25 is m-> */

/*
 * Fundamental addresses
 */
#define	MACHADDR	0x80005000	/* Mach structures */
#define UREGSIZE	0xA0		/* Sizeof(Ureg)+retpc & ur */
#define	MACHP(n)	((Mach *)(MACHADDR+(n)*BY2PG))

/*
 * MMU
 */
#define PGSZ4K		(0x00<<13)
#define PGSZ64K		(0x0F<<13)
#define PGSZ256K	(0x3F<<13)
#define PGSZ1M		(0xFF<<13)
	
#define	KUSEG	0x00000000
#define KSEG0	0x80000000
#define KSEG1	0xA0000000
#define	KSEG2	0xC0000000
#define	KSEG3	0xE0000000
#define	KSEGM	0xE0000000	/* mask to check which seg */

#define PIDXSHFT	12
#define PIDX		(0x7<<PIDXSHFT)
#define	KMAPADDR	0xE1000000
#define KMAPMASK	0xFF000000
#define KMAPSHIFT	15
#define NCOLOR		8
#define getpgcolor(a)	(((ulong)(a)>>PIDXSHFT)&7)

#define	PTEGLOBL	(1<<0)
#define	PTEVALID	(1<<1)
#define	PTEWRITE	(1<<2)
#define PTERONLY	0
#define PTEALGMASK	(7<<3)
#define PTEUNCACHED	(2<<3)
#define PTENONCOHER	(3<<3)
#define PTECOHERXCL	(4<<3)
#define PTECOHERXCLW	(5<<3)
#define PTECOHERUPDW	(6<<3)
#define IOPTE		(PTEGLOBL|PTEVALID|PTEWRITE|PTEUNCACHED)

#define	PTEPID(n)	(n)
#define TLBPID(n)	((n)&0xFF)
#define PTEMAPMEM	(1024*1024)	
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define STLBLOG		13
#define STLBSIZE	(1<<STLBLOG)
#define KPTELOG		8
#define KPTESIZE	(1<<KPTELOG)
#define SEGMAPSIZE	512

#define	NTLBPID	256	/* number of pids */
#define	NTLB	48	/* number of entries */
#define	TLBROFF	4	/* offset of first randomly indexed entry */

/*
 * Address spaces
 */

#define	UZERO	KUSEG			/* base of user address space */
#define	UTZERO	(UZERO+BY2PG)		/* first address in user text */
#define	USTKTOP	(KZERO-BY2PG)		/* byte just beyond user stack */
#define	TSTKTOP	(0xC0000000+USTKSIZE-BY2PG) /* top of temporary stack */
#define TSTKSIZ 100
#define	KZERO	KSEG0			/* base of kernel address space */
#define	KTZERO	(KZERO+0x20000)		/* first address in kernel text */
#define	USTKSIZE	(4*1024*1024)	/* size of user stack */
/*
 * Exception codes
 */
#define	EXCMASK	0x1f		/* mask of all causes */
#define	CINT	 0		/* external interrupt */
#define	CTLBM	 1		/* TLB modification */
#define	CTLBL	 2		/* TLB miss (load or fetch) */
#define	CTLBS	 3		/* TLB miss (store) */
#define	CADREL	 4		/* address error (load or fetch) */
#define	CADRES	 5		/* address error (store) */
#define	CBUSI	 6		/* bus error (fetch) */
#define	CBUSD	 7		/* bus error (data load or store) */
#define	CSYS	 8		/* system call */
#define	CBRK	 9		/* breakpoint */
#define	CRES	10		/* reserved instruction */
#define	CCPU	11		/* coprocessor unusable */
#define	COVF	12		/* arithmetic overflow */
#define	CTRAP	13		/* trap */
#define	CVCEI	14		/* virtual coherence exception (instruction) */
#define	CFPE	15		/* floating point exception */
#define	CWATCH	23		/* watch exception */
#define	CVCED	31		/* virtual coherence exception (data) */

#define isphys(x) (((ulong)x&KSEGM)==KSEG0 || ((ulong)x&KSEGM)==KSEG1)