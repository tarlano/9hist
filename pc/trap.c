#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"

/*
 *  8259 interrupt controllers
 */
enum
{
	Int0ctl=	0x20,		/* control port (ICW1, OCW2, OCW3) */
	Int0aux=	0x21,		/* everything else (ICW2, ICW3, ICW4, OCW1) */
	Int1ctl=	0xA0,		/* control port */
	Int1aux=	0xA1,		/* everything else (ICW2, ICW3, ICW4, OCW1) */

	EOI=		0x20,		/* non-specific end of interrupt */
};

int	int0mask = 7;		/* interrupts enabled for first 8259 */
int	int1mask = 7;		/* interrupts enabled for second 8259 */

/*
 *  trap/interrupt gates
 */
Segdesc ilt[256];
void	(*ivec[256])(void*);

void
sethvec(int v, void (*r)(void), int type, int pri)
{
	ilt[v].d0 = ((ulong)r)&0xFFFF|(KESEL<<16);
	ilt[v].d1 = ((ulong)r)&0xFFFF0000|SEGP|SEGPL(pri)|type;
}

void
setvec(int v, void (*r)(Ureg*), int type)
{
	ilt[v].d1 &= ~SEGTYPE;
	ilt[v].d1 |= type;
	ivec[v] = r;

	/*
	 *  enable corresponding interrupt in 8259
	 */
	if((v&~0x7) == Int0vec){
		int0mask &= ~(1<<(v&7));
		outb(Int0aux, int0mask);
	}
}

/*
 *  set up the interrupt/trap gates
 */
void
trapinit(void)
{
	int i;

	/*
	 *  set the standard traps
	 */
	sethvec(0, intr0, SEGTG, 0);
	sethvec(1, intr1, SEGTG, 0);
	sethvec(2, intr2, SEGTG, 0);
	sethvec(3, intr3, SEGTG, 0);
	sethvec(4, intr4, SEGTG, 0);
	sethvec(5, intr5, SEGTG, 0);
	sethvec(6, intr6, SEGTG, 0);
	sethvec(7, intr7, SEGTG, 0);
	sethvec(8, intr8, SEGTG, 0);
	sethvec(9, intr9, SEGTG, 0);
	sethvec(10, intr10, SEGTG, 0);
	sethvec(11, intr11, SEGTG, 0);
	sethvec(12, intr12, SEGTG, 0);
	sethvec(13, intr13, SEGTG, 0);
	sethvec(14, intr14, SEGTG, 0);
	sethvec(15, intr15, SEGTG, 0);
	sethvec(16, intr16, SEGTG, 0);
	sethvec(17, intr17, SEGTG, 0);
	sethvec(18, intr18, SEGTG, 0);
	sethvec(19, intr19, SEGTG, 0);
	sethvec(20, intr20, SEGTG, 0);
	sethvec(21, intr21, SEGTG, 0);
	sethvec(22, intr22, SEGTG, 0);
	sethvec(23, intr23, SEGTG, 0);

	/*
	 *  set all others to unknown
	 */
	for(i = 24; i < 256; i++)
		sethvec(i, intrbad, SEGIG, 0);

	/*
	 *  system calls
	 */
	sethvec(64, intr64, SEGTG, 3);
/*	setvec(64, syscall, SEGTG);	/**/

	/*
	 *  tell the hardware where the table is (and how long)
	 */
	lidt(ilt, sizeof(ilt));

	/*
	 *  Set up the first 8259 interrupt processor.
	 *  Make 8259 interrupts start at CPU vector Int0vec.
	 *  Set the 8259 as master with edge triggered
	 *  input with fully nested interrupts.
	 */
	outb(Int0ctl, 0x11);		/* ICW1 - edge triggered, master,
					   ICW4 will be sent */
	outb(Int0aux, Int0vec);		/* ICW2 - interrupt vector offset */
	outb(Int0aux, 0x04);		/* ICW3 - master level 2 */
	outb(Int0aux, 0x01);		/* ICW4 - 8086 mode, not buffered */
}

/*
 *  All traps
 */
void
trap(Ureg *ur)
{
	if(ur->trap>=256 || ivec[ur->trap] == 0)
		panic("bad trap type %d %lux\n", ur->trap, ur->pc);

	/*
	 *  call the trap routine
	 */
	(*ivec[ur->trap])(ur);

	/*
	 *  tell the 8259 that we're done with the
	 *  highest level interrupt
	 */
	outb(Int0ctl, EOI);
}

/*
 *  system calls
 */
long
syscall(Ureg *ur)
{
	panic("syscall");
}

void
dumpstack(void)
{
}

void
execpc(ulong entry)
{
	((Ureg*)UREGADDR)->pc = entry;
}
