#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"ureg.h"

typedef struct Clock0link Clock0link;
typedef struct Clock0link {
	void		(*clock)(void);
	Clock0link*	link;
} Clock0link;

static Clock0link *clock0link;
static Lock clock0lock;
ulong	clkrelinq;
void	(*kproftick)(ulong);	/* set by devkprof.c when active */
long	clkvv;
ulong	clkhigh;

void
addclock0link(void (*clock)(void))
{
	Clock0link *lp;

	if((lp = malloc(sizeof(Clock0link))) == 0){
		print("addclock0link: too many links\n");
		return;
	}
	ilock(&clock0lock);
	lp->clock = clock;
	lp->link = clock0link;
	clock0link = lp;
	iunlock(&clock0lock);
}

void
delay(int l)
{
	ulong i, j;

	j = m->delayloop;
	while(l-- > 0)
		for(i=0; i < j; i++)
			;
}

void
microdelay(int l)
{
	ulong i;

	l *= m->delayloop;
	l /= 1000;
	if(l <= 0)
		l = 1;
	for(i = 0; i < l; i++)
		;
}

enum {
	Timebase = 4,	/* system clock cycles per time base cycle */
};

static	ulong	clkreload;

void
clockinit(void)
{
	long x, est;

	est = m->clockgen/1000;	/* initial estimate */
	m->delayloop = est;
	do {
		x = gettbl();
		delay(10);
		x = gettbl() - x;
	} while(x < 0);

	/*
	 *  fix count
	 */
	m->delayloop = (m->delayloop*10*est)/(x*Timebase);
	if(m->delayloop == 0)
		m->delayloop = 1;

	clkreload = (m->clockgen/Timebase)/HZ-1;
	putdec(clkreload);
}

void
clockintr(Ureg *ur)
{
	Clock0link *lp;
	long v;

	v = (clkvv = -getdec());
	if(v > clkreload)
		clkhigh++;
	if(v > clkreload/2){
		if(v > clkreload)
			m->ticks += v/clkreload;
		v = 0;
	}
	putdec(clkreload-v);

	// keep alive for watchdog
	m->iomem->swsr = 0x556c;
	m->iomem->swsr = 0xaa39;
	
	m->ticks++;
//	if(m->ticks%MS2TK(1000) == 0)
//		*(uchar*)(NIMMEM+0x2200) = (m->ticks/MS2TK(1000))&2;

	if(up)
		up->pc = ur->pc;

	checkalarms();
	if(m->machno == 0) {
		if(kproftick != nil)
			(*kproftick)(ur->pc);
		lock(&clock0lock);
		for(lp = clock0link; lp; lp = lp->link)
			lp->clock();
		unlock(&clock0lock);
	}

	if(up && up->state == Running){
//		if(up->type == Interp && tready())
//			ur->cr |= 1<<(31-31);
		if(nrdy > 0)
			sched();
	}
}

void
clkprint(void)
{
	print("clkr=%ld clkvv=%ld clkhigh=%lud\n", clkreload, clkvv, clkhigh);
	print("\n");
}

uvlong
fastticks(uvlong *hz)
{
	if(hz)
		*hz = HZ;
	return m->ticks;
}