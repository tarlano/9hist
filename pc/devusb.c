/*
 * UHCI USB driver
 *	(c) 1998, 1999 C H Forsyth, forsyth@caldo.demon.co.uk
 * to do:
 *	endpoint open/close
 *	build Endpt on open from attributes stored in Udev?
 *	build data0/data1 rings for bulk and interrupt endpoints
 *	endpoint TD rings (can there be prefetch?)
 *	hubs?
 *	special handling of isochronous traffic?
 *	is use of Queues justified? (could have client clean TD rings on wakeup)
 *	bandwidth check
 */

#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"

/*
#ifndef Inferno
#include	"devtab.h"
#include	"brazilia.h"
#endif
*/

#define Pprint
#define Chatty	1
#define DPRINT if(Chatty)print
#define XPRINT if(debug)print

static int debug = 1;

/*
 * USB packet definitions
 */
enum {
	TokIN = 0x69,
	TokOUT = 0xE1,
	TokSETUP = 0x2D,

	/* request type */
	RH2D = 0<<7,
	RD2H = 1<<7,
	Rstandard = 0<<5,
	Rclass = 1<<5,
	Rvendor = 2<<5,
	Rdevice = 0,
	Rinterface = 1,
	Rendpt = 2,
	Rother = 3,
};

typedef uchar byte;

typedef struct Ctlr Ctlr;
typedef struct Endpt Endpt;
typedef struct QTree QTree;
typedef struct Udev Udev;

/*
 * UHCI hardware structures, aligned on 16-byte boundary
 */
typedef struct QH QH;
typedef struct TD TD;

struct TD {
	ulong	link;
	ulong	status;	/* controller r/w */
	ulong	dev;
	ulong	buffer;

	/* software */
	ulong	flags;
	Block*	bp;
	Endpt*	ep;
	TD*	next;
};
#define	TFOL(p)	((TD*)KADDR((ulong)(p) & ~0xF))

struct QH {
	ulong	head;
	ulong	entries;	/* address of next TD or QH to process (updated by controller) */

	/* software */
	union {
		TD*	first;
		QH*	next;		/* free list */
	};
	TD*	last;
};
#define	QFOL(p)	((QH*)KADDR((ulong)(p) & ~0xF))

/*
 * UHCI interface registers and bits
 */
enum {
	/* i/o space */
	Cmd = 0,
	Status = 2,
	Usbintr = 4,
	Frnum = 6,
	Flbaseadd = 8,
	SOFMod = 0xC,
	Portsc0 = 0x10,
	Portsc1 = 0x12,

	/* port status */
	Suspend = 1<<12,
	PortReset = 1<<9,
	SlowDevice = 1<<8,
	ResumeDetect = 1<<6,
	PortChange = 1<<3,	/* write 1 to clear */
	PortEnable = 1<<2,
	StatusChange = 1<<1,	/* write 1 to clear */
	DevicePresent = 1<<0,

	FRAMESIZE=	4096,	/* fixed by hardware; aligned to same */
	NFRAME = 	FRAMESIZE/4,

	Vf = 1<<2,	/* TD only */
	IsQH = 1<<1,
	Terminate = 1<<0,

	/* TD.status */
	SPD = 1<<29,
	ErrLimit0 = 0<<27,
	ErrLimit1 = 1<<27,
	ErrLimit2 = 2<<27,
	ErrLimit3 = 3<<27,
	LowSpeed = 1<<26,
	IsoSelect = 1<<25,
	IOC = 1<<24,
	Active = 1<<23,
	Stalled = 1<<22,
	DataBufferErr = 1<<21,
	Babbling = 1<<20,
	NAKed = 1<<19,
	CRCorTimeout = 1<<18,
	BitstuffErr = 1<<17,
	AnyError = (Stalled | DataBufferErr | Babbling | NAKed | CRCorTimeout | BitstuffErr),

	/* TD.dev */
	IsDATA1 =	1<<19,

	/* TD.flags (software) */
	CancelTD=	1<<0,
};

/*
 * software structures
 */
struct QTree {
	QLock;
	int	nel;
	int	depth;
	QH*	root;
	ulong*	bw;
};

#define	GET2(p)	((((p)[1]&0xFF)<<8)|((p)[0]&0xFF))
#define	PUT2(p,v)	(((p)[0] = (v)), ((p)[1] = (v)>>8))

/*
 * active USB device
 */
struct Udev {
	Ref;
	Lock;
	int	x;	/* index in usbdev[] */
	int	busy;
	int	state;
	int	id;
	byte	port;		/* port number on connecting hub */
	byte	class;
	byte	subclass;
	byte	proto;
	int	ls;
	int	npt;
	Endpt*	ep[16];		/* active end points */
	Udev*	ports;	/* active ports, if hub */
	Udev*	next;		/* next device on this hub */
};

/* device parameters */
enum {
	/* Udev.state */
	Disabled = 0,
	Attached,
	Enabled,
	Assigned,
	Configured,

	/* Udev.class */
	Noclass = 0,
	Hubclass = 9,
};

static char *devstates[] = {
	[Disabled]	"Disabled",
	[Attached] "Attached",
	[Enabled] "Enabled",
	[Assigned] "Assigned",
	[Configured] "Configured",
};

/*
 * device endpoint
 */
struct Endpt {
	Ref;
	Lock;
	int		x;	/* index in Udev.ep */
	int		id;		/* hardware endpoint address */
	int		maxpkt;	/* maximum packet size (from endpoint descriptor) */
	int		data01;	/* 0=DATA0, 1=DATA1 */
	byte	eof;
	byte	class;
	byte	subclass;
	byte	proto;
	byte	mode;	/* OREAD, OWRITE, ORDWR */
	byte	nbuf;	/* number of buffers allowed */
	byte	periodic;
	byte	iso;
	byte	debug;
	byte	active;	/* listed for examination by interrupts */
	int		sched;	/* schedule index; -1 if undefined or aperiodic */
	int		setin;
	ulong	bw;	/* bandwidth requirement */
	int		pollms;	/* polling interval in msec */
	QH*		epq;	/* queue of TDs for this endpoint */

	QLock	rlock;
	Rendez	rr;
	Queue*	rq;
	QLock	wlock;
	Rendez	wr;
	Queue*	wq;

	int		ntd;
	char*	err;

	Udev*	dev;	/* owning device */

	Endpt*	activef;	/* active endpoint list */

	ulong	nbytes;
	ulong	nblocks;
};

struct Ctlr {
	Lock;	/* protects state shared with interrupt (eg, free list) */
	int	io;
	ulong*	frames;	/* frame list */
	int	idgen;	/* version number to distinguish new connections */
	QLock	resetl;	/* lock controller during USB reset */

	TD*	tdpool;
	TD*	freetd;
	QH*	qhpool;
	QH*	freeqh;

	QTree*	tree;	/* tree for periodic Endpt i/o */
	QH*	ctlq;	/* queue for control i/o */
	QH*	bwsop;	/* empty bandwidth sop (to PIIX4 errata specifications) */
	QH*	bulkq;	/* queue for bulk i/o (points back to bandwidth sop) */

	Udev*	ports[2];
};
#define	IN(x)	ins(ub->io+(x))
#define	OUT(x, v)	outs(ub->io+(x), (v))

static	Ctlr	ubus;
static	char	Estalled[] = "usb endpoint stalled";

static	QLock	usbstate;	/* protects name space state */
static	Udev*	usbdev[32];
static struct {
	Lock;
	Endpt*	f;
} activends;

static long readusb(Endpt*, void*, long);
static long writeusb(Endpt*, void*, long, int);

static TD *
alloctd(Ctlr *ub)
{
	TD *t;

	ilock(ub);
	t = ub->freetd;
	if(t == nil)
		panic("alloctd");	/* TO DO */
	ub->freetd = t->next;
	t->next = nil;
	iunlock(ub);
	t->ep = nil;
	t->bp = nil;
	t->status = 0;
	t->link = Terminate;
	t->buffer = 0;
	t->flags = 0;
	return t;
}

static void
freetd(TD *t)
{
	Ctlr *ub;

	ub = &ubus;
	t->ep = nil;
	if(t->bp)
		freeb(t->bp);
	t->bp = nil;
	ilock(ub);
	t->buffer = 0xdeadbeef;
	t->next = ub->freetd;
	ub->freetd = t;
	iunlock(ub);
}

static void
dumpdata(Block *b, int n)
{
	int i;

	XPRINT("\tb %8.8lux[%d]: ", (ulong)b->rp, n);
	if(n > 16)
		n = 16;
	for(i=0; i<n; i++)
		XPRINT(" %2.2ux", b->rp[i]);
	XPRINT("\n");
}

static void
dumptd(TD *t, int follow)
{
	int i, n;
	char buf[20], *s;
	TD *t0;

	t0 = t;
	while(t){
		i = t->dev & 0xFF;
		if(i == TokOUT || i == TokSETUP)
			n = ((t->dev>>21) + 1) & 0x7FF;
		else if((t->status & Active) == 0)
			n = (t->status + 1) & 0x7FF;
		else
			n = 0;
		s = buf;
		if(t->status & Active)
			*s++ = 'A';
		if(t->status & Stalled)
			*s++ = 'S';
		if(t->status & DataBufferErr)
			*s++ = 'D';
		if(t->status & Babbling)
			*s++ = 'B';
		if(t->status & NAKed)
			*s++ = 'N';
		if(t->status & CRCorTimeout)
			*s++ = 'T';
		if(t->status & BitstuffErr)
			*s++ = 'b';
		if(t->status & LowSpeed)
			*s++ = 'L';
		*s = 0;
		XPRINT("td %8.8lux: l=%8.8lux s=%8.8lux d=%8.8lux b=%8.8lux %8.8lux f=%8.8lux\n",
			t, t->link, t->status, t->dev, t->buffer, t->bp?(ulong)t->bp->rp:0, t->flags);
		XPRINT("\ts=%s,ep=%ld,d=%ld,D=%ld\n", buf, (t->dev>>15)&0xF, (t->dev>>8)&0xFF, (t->dev>>19)&1);
		if(t->bp)
			dumpdata(t->bp, n);
		if(!follow || t->link & Terminate || t->link & IsQH)
			break;
		t = TFOL(t->link);
		if(t == t0)
			break;	/* looped */
	}
}

static TD *
alloctde(Endpt *e, int pid, int n)
{
	TD *t;
	int tog, id;

	t = alloctd(&ubus);
	id = (e->x<<7)|(e->dev->x&0x7F);
	tog = 0;
	if(e->data01 && pid != TokSETUP)
		tog = IsDATA1;
	t->ep = e;
	t->status = ErrLimit3 | Active | IOC;	/* or put IOC only on last? */
	if(e->dev->ls)
		t->status |= LowSpeed;
	t->dev = ((n-1)<<21) | ((id&0x7FF)<<8) | pid | tog;
	return t;
}

static QH *
allocqh(Ctlr *ub)
{
	QH *qh;

	ilock(ub);
	qh = ub->freeqh;
	if(qh == nil)
		panic("allocqh");	/* TO DO */
	ub->freeqh = qh->next;
	qh->next = nil;
	iunlock(ub);
	qh->head = Terminate;
	qh->entries = Terminate;
	qh->first = nil;
	qh->last = nil;
	return qh;
}

static void
freeqh(Ctlr *ub, QH *qh)
{
	ilock(ub);
	qh->next = ub->freeqh;
	ub->freeqh = qh;
	iunlock(ub);
}

static void
dumpqh(QH *q)
{
	int i;
	QH *q0;

	q0 = q;
	for(i = 0; q != nil && i < 10; i++){
		XPRINT("qh %8.8lux: %8.8lux %8.8lux\n", q, q->head, q->entries);
		if((q->entries & Terminate) == 0)
			dumptd(TFOL(q->entries), 1);
		if(q->head & Terminate)
			break;
		if((q->head & IsQH) == 0){
			XPRINT("head:");
			dumptd(TFOL(q->head), 1);
			break;
		}
		q = QFOL(q->head);
		if(q == q0)
			break;	/* looped */
	}
}

static void
queuetd(Ctlr *ub, QH *q, TD *t, int vf)
{
	TD *lt;

	for(lt = t; lt->next != nil; lt = lt->next)
		lt->link = PADDR(lt->next) | vf;
	lt->link = Terminate;
	ilock(ub);
	if(q->first != nil){
		q->last->link = PADDR(t) | vf;
		q->last->next = t;
	}else{
		q->first = t;
		q->entries = PADDR(t);
	}
	q->last = lt;
	dumpqh(q);
	iunlock(ub);
}

static void
cleantd(TD *t, int discard)
{
	Block *b;
	int n, err;

	XPRINT("cleanTD: %8.8lux %8.8lux %8.8lux %8.8lux\n", t->link, t->status, t->dev, t->buffer);
	if(t->ep != nil && t->ep->debug)
		dumptd(t, 0);
	if(t->status & Active)
		panic("cleantd Active");
	err = t->status & (AnyError&~NAKed);
	/* TO DO: on t->status&AnyError, q->entries will not have advanced */
	if (err)
		XPRINT("cleanTD: Error %8.8lux %8.8lux %8.8lux %8.8lux\n", t->link, t->status, t->dev, t->buffer);
	switch(t->dev&0xFF){
	case TokIN:
		if(discard || (t->flags & CancelTD) || t->ep == nil || t->ep->x!=0&&err){
			if(t->ep != nil){
				if(err != 0)
					t->ep->err = err==Stalled? Estalled: Eio;
				wakeup(&t->ep->rr);	/* in case anyone cares */
			}
			break;
		}
		b = t->bp;
		n = (t->status + 1) & 0x7FF;
		if(n > b->lim - b->wp)
			n = 0;
		b->wp += n;
		if(Chatty)
			dumpdata(b, n);
		t->bp = nil;
		t->ep->nbytes += n;
		t->ep->nblocks++;
		qpass(t->ep->rq, b);	/* TO DO: flow control */
		wakeup(&t->ep->rr);	/* TO DO */
		break;
	case TokSETUP:
		XPRINT("cleanTD: TokSETUP %lux\n", &t->ep);
		/* don't really need to wakeup: subsequent IN or OUT gives status */
		if(t->ep != nil) {
			wakeup(&t->ep->wr);	/* TO DO */
			XPRINT("cleanTD: wakeup %lux\n", &t->ep->wr);
		}
		break;
	case TokOUT:
		/* TO DO: mark it done somewhere */
		XPRINT("cleanTD: TokOut %lux\n", &t->ep);
		if(t->ep != nil){
			if(t->bp){
				n = BLEN(t->bp);
				t->ep->nbytes += n;
				t->ep->nblocks++;
			}
			if(t->ep->x!=0 && err != 0)
				t->ep->err = err==Stalled? Estalled: Eio;
			if(--t->ep->ntd < 0)
				panic("cleantd ntd");
			wakeup(&t->ep->wr);	/* TO DO */
			XPRINT("cleanTD: wakeup %lux\n", &t->ep->wr);
		}
		break;
	}
	freetd(t);
}

static void
cleanq(QH *q, int discard, int vf)
{
	TD *t, *tp;
	Ctlr *ub;

	ub = &ubus;
	ilock(ub);
	tp = nil;
	for(t = q->first; t != nil;){
		XPRINT("cleanq: %8.8lux %8.8lux %8.8lux %8.8lux %8.8lux %8.8lux\n", t->link, t->status, t->dev, t->buffer, t->flags, t->next);
		if(t->status & Active){
			if(t->status & NAKed){
				t->status = (t->status & ~NAKed) | IOC;	/* ensure interrupt next frame */
				tp = t;
				t = t->next;
				continue;
			}
			if(t->flags & CancelTD){
				XPRINT("cancelTD: %8.8lux\n", (ulong)t);
				t->status = (t->status & ~Active) | IOC;	/* ensure interrupt next frame */
				tp = t;
				t = t->next;
				continue;
			}
tp = t;
t = t->next;
continue;
			break;
		}
		t->status &= ~IOC;
		if (tp == nil) {
			q->first = t->next;
			if(q->first != nil)
				q->entries = PADDR(q->first);
			else
				q->entries = Terminate;
		} else {
			tp->next = t->next;
			if (t->next != nil)
				tp->link = PADDR(t->next) | vf;
			else
				tp->link = Terminate;
		}
		if (q->last == t)
			q->last = tp;
		iunlock(ub);
		cleantd(t, discard);
		ilock(ub);
		if (tp)
			t = tp->next;
		else
			t = q->first;
		XPRINT("t = %8.8lux\n", t);
		dumpqh(q);
	}
	iunlock(ub);
}

static void
canceltds(Ctlr *ub, QH *q, Endpt *e)
{
	TD *t;

	if(q != nil){
		ilock(ub);
		for(t = q->first; t != nil; t = t->next)
			if(t->ep == e)
				t->flags |= CancelTD;
		iunlock(ub);
		XPRINT("cancel:\n");
		dumpqh(q);
	}
}

static void
eptcancel(Endpt *e)
{
	Ctlr *ub;

	if(e == nil)
		return;
	ub = &ubus;
	canceltds(ub, e->epq, e);
	canceltds(ub, ub->ctlq, e);
	canceltds(ub, ub->bulkq, e);
}

static void
eptactivate(Endpt *e)
{
	ilock(&activends);
	if(e->active == 0){
		e->active = 1;
		e->activef = activends.f;
		activends.f = e;
	}
	iunlock(&activends);
}

static void
eptdeactivate(Endpt *e)
{
	Endpt **l;

	/* could be O(1) but not worth it yet */
	ilock(&activends);
	if(e->active){
		e->active = 0;
		for(l = &activends.f; *l != e; l = &(*l)->activef)
			if(*l == nil){
				iunlock(&activends);
				panic("usb eptdeactivate");
			}
		*l = e->activef;
	}
	iunlock(&activends);
}

static QH*
qxmit(Endpt *e, Block *b, int pid)
{
	TD *t;
	int n, vf;
	Ctlr *ub;
	QH *qh;

	if(b != nil){
		n = BLEN(b);
		t = alloctde(e, pid, n);
		t->bp = b;
		t->buffer = PADDR(b->rp);
	}else
		t = alloctde(e, pid, 0);
	ub = &ubus;
	ilock(ub);
	e->ntd++;
	iunlock(ub);
if(e->debug)pprint("QTD: %8.8lux n=%ld\n", t, b?BLEN(b): 0);
	vf = 0;
	if(e->x == 0){
		qh = ub->ctlq;
		vf = 0;
	}else if((qh = e->epq) == nil || e->mode != OWRITE){
		qh = ub->bulkq;
		vf = Vf;
	}
	queuetd(ub, qh, t, vf);
	return qh;
}

static QH*
qrcv(Endpt *e)
{
	TD *t;
	Block *b;
	Ctlr *ub;
	QH *qh;
	int vf;

XPRINT("qrcv\n");
	t = alloctde(e, TokIN, e->maxpkt);
	b = allocb(e->maxpkt);
	t->bp = b;
	t->buffer = PADDR(b->wp);
	ub = &ubus;
	vf = 0;
	if(e->x == 0){
XPRINT("enq\n");
		qh = ub->ctlq;
	}else if((qh = e->epq) == nil || e->mode != OREAD){
XPRINT("bulkenq\n");
		qh = ub->bulkq;
		vf = Vf;
	}
	queuetd(ub, qh, t, vf);
	return qh;
}

static Block *
usbreq(int type, int req, int value, int offset, int count)
{
	Block *b;

	b = allocb(8);
	b->wp[0] = type;
	b->wp[1] = req;
	PUT2(b->wp+2, value);
	PUT2(b->wp+4, offset);
	PUT2(b->wp+6, count);
	b->wp += 8;
	return b;
}

/*
 * return smallest power of 2 >= n
 */
static int
flog2(int n)
{
	int i;

	for(i=0; (1<<i)<n; i++)
		;
	return i;
}

/*
 * build the periodic scheduling tree:
 * framesize must be a multiple of the tree size
 */
static QTree *
mkqhtree(ulong *frame, int framesize, int maxms)
{
	int i, n, d, o, leaf0, depth;
	QH *tree, *qh;
	QTree *qt;

	depth = flog2(maxms);
	n = (1<<(depth+1))-1;
	qt = mallocz(sizeof(*qt), 1);
	if(qt == nil)
		return nil;
	qt->nel = n;
	qt->depth = depth;
	qt->bw = mallocz(n*sizeof(qt->bw), 1);
	if(qt->bw == nil){
		free(qt);
		return nil;
	}
	tree = xspanalloc(n*sizeof(QH), 16, 0);
	if(tree == nil){
		free(qt);
		return nil;
	}
	qt->root = tree;
	tree->head = Terminate;	/* root */
	tree->entries = Terminate;
	for(i=1; i<n; i++){
		qh = &tree[i];
		qh->head = PADDR(&tree[(i-1)/2]) | IsQH;
		qh->entries = Terminate;
	}
	/* distribute leaves evenly round the frame list */
	leaf0 = n/2;
	for(i=0; i<framesize; i++){
		o = 0;
		for(d=0; d<depth; d++){
			o <<= 1;
			if(i & (1<<d))
				o |= 1;
		}
		if(leaf0+o >= n){
			XPRINT("leaf0=%d o=%d i=%d n=%d\n", leaf0, o, i, n);
			break;
		}
		frame[i] = PADDR(&tree[leaf0+o]) | IsQH;
	}
	return qt;
}

static void
dumpframe(int f, int t)
{
	QH *q, *tree;
	ulong p, *frame;
	int i, n;

	n = ubus.tree->nel;
	tree = ubus.tree->root;
	frame = ubus.frames;
	if(f < 0)
		f = 0;
	if(t < 0)
		t = 32;
	for(i=f; i<t; i++){
		XPRINT("F%.2d %8.8lux %8.8lux\n", i, frame[i], QFOL(frame[i])->head);
		for(p=frame[i]; (p & IsQH) && (p &Terminate) == 0; p = q->head){
			q = QFOL(p);
			if(!(q >= tree && q < &tree[n])){
				XPRINT("Q: p=%8.8lux out of range\n", p);
				break;
			}
			XPRINT("  -> %8.8lux h=%8.8lux e=%8.8lux\n", p, q->head, q->entries);
		}
	}
}

static int
pickschedq(QTree *qt, int pollms, ulong bw, ulong limit)
{
	int i, j, d, ub, q;
	ulong best, worst, total;

	d = flog2(pollms);
	if(d > qt->depth)
		d = qt->depth;
	q = -1;
	worst = 0;
	best = ~0;
	ub = (1<<(d+1))-1;
	for(i=(1<<d)-1; i<ub; i++){
		total = qt->bw[0];
		for(j=i; j > 0; j=(j-1)/2)
			total += qt->bw[j];
		if(total < best){
			best = total;
			q = i;
		}
		if(total > worst)
			worst = total;
	}
	if(worst+bw >= limit)
		return -1;
	return q;
}

static int
schedendpt(Endpt *e)
{
	Ctlr *ub;
	QH *qh;
	int q;

	if(!e->periodic || e->sched >= 0)
		return 0;
	ub = &ubus;
	if(e->epq == nil){
		e->epq = allocqh(ub);
		if(e->epq == nil)
			return -1;
	}
	qlock(ub->tree);
	q = pickschedq(ub->tree, e->pollms, e->bw, ~0);	/* TO DO: bus bandwidth limit */
	if(q < 0)
		return -1;
	ub->tree->bw[q] += e->bw;
	qh = &ub->tree->root[q];
	e->sched = q;
	e->epq->head = qh->entries;
	e->epq->entries = Terminate;
	qh->entries = PADDR(e->epq) | IsQH;
	qunlock(ub->tree);
	return 0;
}

static void
unschedendpt(Endpt *e)
{
	Ctlr *ub;
	ulong p;
	QH *qh;
	int q;

	ub = &ubus;
	if(e->epq == nil || (q = e->sched) < 0)
		return;
	p = PADDR(e->epq) | IsQH;
	qlock(ub->tree);
	ub->tree->bw[q] -= e->bw;
	qh = &ub->tree->root[q];
	for(; qh->entries != p; qh = QFOL(qh->entries))
		if(qh->entries & Terminate || (qh->entries & IsQH) == 0){
			qunlock(ub->tree);
			panic("usb: unschedendpt");
		}
	qh->entries = e->epq->head;
	qunlock(ub->tree);
	e->epq->head = Terminate;
}

static Endpt *
devendpt(Udev *d, int id, int add)
{
	Endpt *e, **p;

	p = &d->ep[id&0xF];
	lock(d);
	if((e = *p) != nil){
		incref(e);
		unlock(d);
		return e;
	}
	unlock(d);
	if(!add)
		return nil;
	e = mallocz(sizeof(*e), 1);
	e->ref = 1;
	e->x = id&0xF;
	e->id = id;
	e->periodic = 0;
	e->sched = -1;
	e->maxpkt = 8;
	e->pollms = 0;
	e->bw = 0;
	e->nbuf = 1;
	e->dev = d;
	e->active = 0;
	e->epq = allocqh(ub);
	if(e->epq == nil)
		panic("devendpt");

	lock(d);
	if(*p != nil){
		incref(*p);
		unlock(d);
		free(e);
		return *p;
	}
	*p = e;
	unlock(d);
	e->rq = qopen(8*1024, 0, nil, e);
	e->wq = qopen(8*1024, 0, nil, e);
	return e;
}

static void
freept(Endpt *e)
{
	if(e != nil && decref(e) == 0){
		XPRINT("freept(%d,%d)\n", e->dev->x, e->x);
		unschedendpt(e);
		e->dev->ep[e->x] = nil;
		eptdeactivate(e);
		if(e->epq != nil)
			freeqh(&ubus, e->epq);
		free(e);
	}
}

static void
usbdevreset(Udev *d)
{
	d->state = Disabled;
	if(d->class == Hubclass)
		for(d = d->ports; d != nil; d = d->next)
			usbdevreset(d);
}

static void
freedev(Udev *d)
{
	int i;

	if(d != nil && decref(d) == 0){
		for(i=0; i<nelem(d->ep); i++)
			freept(d->ep[i]);
		if(d->x >= 0)
			usbdev[d->x] = nil;
		free(d);
	}
}

static void
hubportreset(Udev *h, int p)
{
	USED(h, p);
	/* reset state of each attached device? */
}

static	int	ioports[] = {-1, Portsc0, Portsc1};

static void
portreset(int port)
{
	Ctlr *ub;
	int i, p;

	/* should check that device not being configured on other port? */
	p = ioports[port];
	ub = &ubus;
	qlock(&ub->resetl);
	if(waserror()){
		qunlock(&ub->resetl);
		nexterror();
	}
	XPRINT("r: %x\n", IN(p));
	ilock(ub);
	OUT(p, PortReset);
	delay(12);	/* BUG */
	XPRINT("r2: %x\n", IN(p));
	OUT(p, IN(p) & ~PortReset);
	XPRINT("r3: %x\n", IN(p));
	OUT(p, IN(p) | PortEnable);
	microdelay(64);
	for(i=0; i<1000 && (IN(p) & PortEnable) == 0; i++)
		;
	XPRINT("r': %x %d\n", IN(p), i);
	OUT(p, (IN(p) & ~PortReset)|PortEnable);
	iunlock(ub);
	hubportreset(nil, port);
	poperror();
	qunlock(&ub->resetl);
}

static void
portenable(int port, int on)
{
	Ctlr *ub;
	int w, p;

	/* should check that device not being configured on other port? */
	p = ioports[port];
	ub = &ubus;
	qlock(&ub->resetl);
	if(waserror()){
		qunlock(&ub->resetl);
		nexterror();
	}
	ilock(ub);
	w = IN(p);
	if(on)
		w |= PortEnable;
	else
		w &= ~PortEnable;
	OUT(p, w);
	microdelay(64);
	iunlock(ub);
	XPRINT("e: %x\n", IN(p));
	if(!on)
		hubportreset(nil, port);
	poperror();
	qunlock(&ub->resetl);
}

static int
portinfo(Ctlr *ub, int *p0, int *p1)
{
	int m, v;

	ilock(ub);
	m = 0;
	if((v = IN(Portsc0)) & PortChange){
		OUT(Portsc0, v);
		m |= 1<<0;
	}
	*p0 = v;
	if((v = IN(Portsc1)) & PortChange){
		OUT(Portsc1, v);
		m |= 1<<1;
	}
	*p1 = v;
	iunlock(ub);
	return m;
}

static void
interrupt(Ureg*, void *a)
{
	Ctlr *ub;
	Endpt *e;
	int s;

	ub = a;
	s = IN(Status);
	XPRINT("usbint: #%x f%d\n", s, IN(Frnum));
	if (s & 0x1a) {
		XPRINT("cmd #%x sofmod #%x\n", IN(Cmd), inb(ub->io+SOFMod));
		XPRINT("sc0 #%x sc1 #%x\n", IN(Portsc0), IN(Portsc1));
	}
	OUT(Status, s);

	XPRINT("cleanq(ub->ctlq, 0, 0)\n");
	cleanq(ub->ctlq, 0, 0);
	XPRINT("cleanq(ub->bulkq, 0, Vf)\n");
	cleanq(ub->bulkq, 0, Vf);
	ilock(&activends);
	for(e = activends.f; e != nil; e = e->activef)
		if(e->epq != nil) {
			XPRINT("cleanq(e->epq, 0, 0)\n");
			cleanq(e->epq, 0, 0);
		}
	iunlock(&activends);
}

enum
{
	Qtopdir = 0,
	Q2nd,
	Qbusctl,
	Qnew,
	Qport,
	Q3rd,
	Qctl,
	Qsetup,
	Qdebug,
	Qstatus,
	Qep0,
	/* other endpoint files */
};

/*
 * Qid path is:
 *	 8 bits of file type (qids above)
 *	10 bits of slot number +1; 0 means not attached to device
 */
#define	QSHIFT	8	/* location in qid of device # */
#define	QMASK	((1<<QSHIFT)-1)

#define	QID(q)		((q).path&QMASK)
#define	DEVPATH(q)	(((q)&~CHDIR)>>QSHIFT)

static Dirtab usbdir2[] = {
	"new",	{Qnew},	0,	0666,
	"ctl",		{Qbusctl},	0,	0666,
	"port",	{Qport},	0,	0444,
};

static Dirtab usbdir3[]={
	"ctl",		{Qctl},	0,	0666,
	"setup",	{Qsetup},	0,	0666,
	"status",	{Qstatus},	0,	0444,
	"debug",	{Qdebug},	1,	0666,
	/* epNdata names are generated on demand */
};

static Udev *
usbdeviceofpath(ulong path)
{
	int s;

	s = DEVPATH(path);
	if(s == 0)
		return nil;
	return usbdev[s-1];
}

static Udev *
usbdevice(Chan *c)
{
	Udev *d;

	d = usbdeviceofpath(c->qid.path);
	if(d == nil || d->id != c->qid.vers || d->state == Disabled)
		error(Ehungup);
	return d;
}

static Udev *
usbnewdevice(void)
{
	Udev *d;
	Endpt *e;
	int i;

	d = nil;
	qlock(&usbstate);
	if(waserror()){
		qunlock(&usbstate);
		nexterror();
	}
	for(i=0; i<nelem(usbdev); i++)
		if(usbdev[i] == nil){
			ubus.idgen++;
			d = mallocz(sizeof(*d), 1);
			d->ref = 1;
			d->x = i;
			d->id = (ubus.idgen << 8) | i;
			d->state = Enabled;
			e = devendpt(d, 0, 1);	/* always provide control endpoint 0 */
			e->mode = ORDWR;
			e->periodic = 0;
			e->sched = -1;
			usbdev[i] = d;
			break;
		}
	poperror();
	qunlock(&usbstate);
	return d;
}

static void
usbreset(void)
{
	Ctlr *ub;
	Pcidev *cfg;
	int i;
	ulong port;
	QTree *qt;
	TD *t;

	ub = &ubus;
	memset(&cfg, 0, sizeof(cfg));
	cfg = pcimatch(0, 0x8086, 0x7112);	/* Intel chipset PIIX 4*/
	if(cfg == nil) {
//		cfg = pcimatch(0, 0x8086, 0x7112);	/* Intel chipset PIIX 3*/
//		if(cfg == nil) {
			cfg = pcimatch(0, 0x1106, 0x0586);	/* Via chipset */
			if(cfg == nil) {
				DPRINT("No USB device found\n");
				return;
			}
//		}
	}
	port = cfg->mem[4].bar & ~0x0F;
	if (port == 0) {
		print("usb: failed to map registers\n");
		return;
	}

	print("USB: %x/%x port 0x%lux size 0x%x irq %d\n",
		cfg->vid, cfg->did, port, cfg->mem[4].size, cfg->intl);

	i = inb(port+SOFMod);
	if(0){
		OUT(Cmd, 4);	/* global reset */
		delay(15);
		OUT(Cmd, 0);	/* end reset */
		delay(4);
	}
	outb(port+SOFMod, i);
	// Interrupt handler
	intrenable(cfg->intl, interrupt, ub, cfg->tbdf, "usb");

	ub->io = port;
	ub->tdpool = xspanalloc(128*sizeof(TD), 16, 0);
	for(i=128; --i>=0;){
		ub->tdpool[i].next = ub->freetd;
		ub->freetd = &ub->tdpool[i];
	}
	ub->qhpool = xspanalloc(32*sizeof(QH), 16, 0);
	for(i=32; --i>=0;){
		ub->qhpool[i].next = ub->freeqh;
		ub->freeqh = &ub->qhpool[i];
	}

	/*
	 * the root of the periodic (interrupt & isochronous) scheduling tree
	 * points to the control queue and the bandwidth sop for bulk traffic.
	 * this is looped following the instructions in PIIX4 errata 29773804.pdf:
	 * a QH links to a looped but inactive TD as its sole entry,
	 * with its head entry leading on to the bulk traffic, the last QH of which
	 * links back to the empty QH.
	 */
	ub->bulkq = allocqh(ub);
	t = alloctd(ub);	/* inactive TD, looped */
	t->link = PADDR(t);
	ub->bwsop = allocqh(ub);
	ub->bwsop->head = PADDR(ub->bulkq) | IsQH;
	ub->bwsop->entries = PADDR(t);
	ub->ctlq = allocqh(ub);
	ub->ctlq->head = PADDR(ub->bwsop) | IsQH;
	ub->bulkq->head = PADDR(ub->bwsop) | IsQH;	/* loop back */
	print("usbcmd\t0x%.4x\nusbsts\t0x%.4x\nusbintr\t0x%.4x\nfrnum\t0x%.2x\n",
		IN(Cmd), IN(Status), IN(Usbintr), inb(port+Frnum));
	print("frbaseadd\t0x%.4x\nsofmod\t0x%x\nportsc1\t0x%.4x\nportsc2\t0x%.4x\n",
		IN(Flbaseadd), inb(port+SOFMod), IN(Portsc0), IN(Portsc1));
	OUT(Cmd, 0);	/* stop */
	ub->frames = xspanalloc(FRAMESIZE, FRAMESIZE, 0);
	qt = mkqhtree(ub->frames, NFRAME, 32);
	if(qt == nil){
		print("usb: can't allocate scheduling tree\n");
		ub->io = 0;
		return;
	}
	qt->root->head = PADDR(ub->ctlq) | IsQH;
	ub->tree = qt;
	print("usb tree: nel=%d depth=%d\n", qt->nel, qt->depth);

	outl(port+Flbaseadd, PADDR(ub->frames));
	OUT(Frnum, 0);
	OUT(Usbintr, 0xF);	/* enable all interrupts */
	print("cmd 0x%x sofmod 0x%x\n", IN(Cmd), inb(port+SOFMod));
	print("sc0 0x%x sc1 0x%x\n", IN(Portsc0), IN(Portsc1));
}

void
usbinit(void)
{
	Udev *d;

	if(ubus.io != 0 && usbdev[0] == nil){
		d = usbnewdevice();	/* reserve device 0 for configuration */
		incref(d);
		d->state = Attached;
	}
}

Chan *
usbattach(char *spec)
{
	Ctlr *ub;

	ub = &ubus;
	if(ub->io == 0)
		error(Enodev);
	if((IN(Cmd)&1)==0 || *spec)
		OUT(Cmd, 1);	/* run */
//	pprint("at: c=%x s=%x c0=%x\n", IN(Cmd), IN(Status), IN(Portsc0));
	return devattach('U', spec);
}

Chan *
usbclone(Chan *c, Chan *nc)
{
	return devclone(c, nc);
}

static int
usbgen(Chan *c, Dirtab*, int, int s, Dir *dp)
{
	int t;
	Qid q;
	ulong path;
	Udev *d;
	Dirtab *tab;
	Endpt *e;
	char buf[NAMELEN];

	/*
	 * Top level directory contains the name of the device.
	 */
	if(c->qid.path == CHDIR){
		if(s == 0){
			devdir(c, (Qid){CHDIR|Q2nd, 0}, "usb", 0, eve, 0555, dp);
			return 1;
		}
		return -1;
	}

	/*
	 * Second level contains "new" plus all the clients.
	 */
	t = QID(c->qid);
	if(t < Q3rd){
		if(s < nelem(usbdir2)){
			tab = &usbdir2[s];
			devdir(c, tab->qid, tab->name, tab->length, eve, tab->perm, dp);
			return 1;
		}
		if((s -= nelem(usbdir2)) >= 0 && s < nelem(usbdev)){
			d = usbdev[s];
			if(d == nil)
				return 0;
			sprint(buf, "%d", s);
			q = (Qid){CHDIR|((s+1)<<QSHIFT)|Q3rd, d->id};
			devdir(c, q, buf, 0, eve, 0555, dp);
			return 1;
		}
		return -1;
	}

	/*
	 * Third level.
	 */
	path = c->qid.path&~(CHDIR|QMASK);	/* slot component */
	q.vers = c->qid.vers;
	if(s < nelem(usbdir3)){
		Dirtab *tab = &usbdir3[s];
		q.path = path | tab->qid.path;
		devdir(c, q, tab->name, tab->length, eve, tab->perm, dp);
		return 1;
	}

	/* active endpoints */
	d = usbdeviceofpath(path);
	if(d == nil)
		return -1;
	s -= nelem(usbdir3);
	if(s < 0 || s >= nelem(d->ep))
		return -1;
	if(s == 0 || (e = d->ep[s]) == nil)	/* ep0data is called "setup" */
		return 0;
	sprint(buf, "ep%ddata", s);
	q.path = path | (Qep0+s);
	devdir(c, q, buf, 0, eve, e->mode==OREAD? 0444: e->mode==OWRITE? 0222: 0666, dp);
	return 1;
}

int
usbwalk(Chan *c, char *name)
{
	if(strcmp(name, "..") == 0){
		switch(QID(c->qid)){
		case Qtopdir:
			return 1;
		case Q2nd:
			c->qid = (Qid){CHDIR|Qtopdir, 0};
			break;
		case Q3rd:
			c->qid = (Qid){CHDIR|Q2nd, 0};
			break;
		default:
			panic("usbwalk %lux", c->qid.path);
		}
		return 1;
	}
	return devwalk(c, name, nil, 0, usbgen);
}

void
usbstat(Chan *c, char *db)
{
	devstat(c, db, nil, 0, usbgen);
}

Chan *
usbopen(Chan *c, int omode)
{
	Udev *d;
	int f, s;

	if(c->qid.path & CHDIR)
		return devopen(c, omode, nil, 0, usbgen);

	f = 0;
	if(QID(c->qid) == Qnew){
		d = usbnewdevice();
		if(d == nil)
			error(Enodev);
		c->qid.path = Qctl|((d->x+1)<<QSHIFT);
		c->qid.vers = d->id;
		f = 1;
	}

	if(c->qid.path < Q3rd)
		return devopen(c, omode, nil, 0, usbgen);

	qlock(&usbstate);
	if(waserror()){
		qunlock(&usbstate);
		nexterror();
	}

	switch(QID(c->qid)){
	case Qctl:
		d = usbdevice(c);
		if(0&&d->busy)
			error(Einuse);
		d->busy = 1;
		if(!f)
			incref(d);
		break;

	default:
		d = usbdevice(c);
		s = QID(c->qid) - Qep0;
		if(s >= 0 && s < nelem(d->ep)){
			Endpt *e;
			if((e = d->ep[s]) == nil)
				error(Enodev);
			if(schedendpt(e) < 0)
				error("can't schedule USB endpoint");
			eptactivate(e);
		}
		incref(d);
		break;
	}
	poperror();
	qunlock(&usbstate);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

void
usbcreate(Chan *c, char *name, int omode, ulong perm)
{
	USED(c, name, omode, perm);
	error(Eperm);
}

void
usbremove(Chan*)
{
	error(Eperm);
}

void
usbwstat(Chan *c, char *dp)
{
	USED(c, dp);
	error(Eperm);
}

void
usbclose(Chan *c)
{
	Udev *d;

	if(c->qid.path & CHDIR || c->qid.path < Q3rd)
		return;
	qlock(&usbstate);
	if(waserror()){
		qunlock(&usbstate);
		nexterror();
	}
	d = usbdevice(c);
	if(QID(c->qid) == Qctl)
		d->busy = 0;
	if(c->flag & COPEN)
		freedev(d);
	poperror();
	qunlock(&usbstate);
}

static int
eptinput(void *arg)
{
	Endpt *e;

	e = arg;
	return e->eof || e->err || qcanread(e->rq);
}

static long
readusb(Endpt *e, void *a, long n)
{
	Block *b;
	uchar *p;
	long l, i;

	XPRINT("qlock(%p)\n", &e->rlock);
	qlock(&e->rlock);
	XPRINT("got qlock(%p)\n", &e->rlock);
	if(waserror()){
		qunlock(&e->rlock);
		eptcancel(e);
		nexterror();
	}
	p = a;
	do {
		if(e->eof) {
			XPRINT("e->eof\n");
			break;
		}
		if(e->err)
			error(e->err);
		qrcv(e);
		if(!e->iso)
			e->data01 ^= 1;
		sleep(&e->rr, eptinput, e);
		if(e->err)
			error(e->err);
		b = qget(e->rq);	/* TO DO */
		if(b == nil) {
			XPRINT("b == nil\n");
			break;
		}
		if(waserror()){
			freeb(b);
			nexterror();
		}
		l = BLEN(b);
		if((i = l) > n)
			i = n;
		if(i > 0){
			memmove(p, b->rp, i);
			p += i;
		}
		poperror();
		freeb(b);
		n -= i;
	} while (l == e->maxpkt && n > 0);
	poperror();
	qunlock(&e->rlock);
	return p-(uchar*)a;
}

long
usbread(Chan *c, void *a, long n, vlong offset)
{
	Endpt *e;
	Udev *d;
	char buf[48], *s;
	int t, w0, w1, ps, l, i;

	if(c->qid.path & CHDIR)
		return devdirread(c, a, n, nil, 0, usbgen);

	t = QID(c->qid);
	switch(t){
	case Qbusctl:
		snprint(buf, sizeof(buf), "%11d %11d ", 0, 0);
		return readstr(offset, a, n, buf);

	case Qport:
		ps = portinfo(&ubus, &w0, &w1);
		snprint(buf, sizeof(buf), "0x%ux 0x%ux 0x%ux ", ps, w0, w1);
		return readstr(offset, a, n, buf);

	case Qctl:
		d = usbdevice(c);
		sprint(buf, "%11d %11d ", d->x, d->id);
		return readstr(offset, a, n, buf);

	case Qsetup:	/* endpoint 0 */
		d = usbdevice(c);
		if((e = d->ep[0]) == nil)
			error(Eio);	/* can't happen */
		e->data01 = 1;
		n = readusb(e, a, n);
		if(e->setin){
			e->setin = 0;
			e->data01 = 1;
			writeusb(e, "", 0, TokOUT);
		}
		break;

	case Qdebug:
		n=0;
		break;

	case Qstatus:
		d = usbdevice(c);
		s = smalloc(READSTR);
		if(waserror()){
			free(s);
			nexterror();
		}
		l = snprint(s, READSTR, "%s %d %d %d\n", devstates[d->state], d->class, d->subclass, d->proto);
		for(i=0; i<nelem(d->ep); i++)
			if((e = d->ep[i]) != nil)	/* TO DO: freeze e */
				l += snprint(s+l, READSTR-l, "%d bytes %lud blocks %lud\n", i, e->nbytes, e->nblocks);
		n = readstr(offset, a, n, s);
		poperror();
		free(s);
		break;

	default:
		d = usbdevice(c);
		if((t -= Qep0) < 0 || t >= nelem(d->ep))
			error(Eio);
		if((e = d->ep[t]) == nil || e->mode == OWRITE)
			error(Eio);	/* can't happen */
		n=readusb(e, a, n);
		break;
	}
	return n;
}

static int
qisempty(void *arg)
{
	return ((QH*)arg)->entries & Terminate;
}

static long
writeusb(Endpt *e, void *a, long n, int tok)
{
	long i;
	Block *b;
	uchar *p;
	QH *qh;

	p = a;
	qlock(&e->wlock);
	if(waserror()){
		qunlock(&e->wlock);
		eptcancel(e);
		nexterror();
	}
	do {
		int j;

		if(e->err)
			error(e->err);
		if((i = n) >= e->maxpkt)
			i = e->maxpkt;
		b = allocb(i);
		if(waserror()){
			freeb(b);
			nexterror();
		}
		XPRINT("out [%ld]", i);
		for (j = 0; j < i; j++) XPRINT(" %.2x", p[j]);
		XPRINT("\n");
		memmove(b->wp, p, i);
		b->wp += i;
		p += i;
		n -= i;
		poperror();
		qh = qxmit(e, b, tok);
		tok = TokOUT;
		if(!e->iso)
			e->data01 ^= 1;
		if(e->ntd >= e->nbuf) {
			XPRINT("writeusb: sleep %lux\n", &e->wr);
			sleep(&e->wr, qisempty, qh);
			XPRINT("writeusb: awake\n");
		}
	} while(n > 0);
	poperror();
	qunlock(&e->wlock);
	return p-(uchar*)a;
}

int
getfields(char *ss, char **sp, int nptrs)
{
	uchar *s = (uchar*)ss;
	uchar **p = (uchar**)sp;
	uint c;

	c = 0;
	for(;;){
		if(--nptrs < 0)
			break;
		*p++ = s;
		do {
			c = *s++;
			if (c == ' ') break;
			if (c == '\t') break;
			if (c == '\n') break;
		} while(c);
		if(c == 0)
			break;
		s[-1] = 0;
	}
	if(nptrs > 0)
		*p = 0;
	else
	if(--s >= (uchar*)ss)
		*s = c;
	return p - (uchar**)sp;
}

long
usbwrite(Chan *c, void *a, long n, vlong)
{
	Udev *d;
	Endpt *e;
	int id, nw, nf, t, i;
	char cmd[50], *fields[10];

	if(c->qid.path & CHDIR)
		error(Egreg);
	t = QID(c->qid);
	if(t == Qbusctl){
		if(n >= sizeof(cmd)-1)
			n = sizeof(cmd)-1;
		memmove(cmd, a, n);
		cmd[n] = 0;
		nf = getfields(cmd, fields, nelem(fields));
		if(nf==1 && strcmp(fields[0], "dump")==0){
			dumpframe(-1, -1);
			return n;
		}
		if(nf < 2)
			error(Ebadarg);
		id = strtol(fields[1], nil, 0);
		if(id != 1 && id != 2)
			error(Ebadarg);	/* there are two ports on the root hub */
		if(strcmp(fields[0], "reset") == 0)
			portreset(id);
		else if(strcmp(fields[0], "enable") == 0)
			portenable(id, 1);
		else if(strcmp(fields[0], "disable") == 0)
			portenable(id, 0);
		else
			error(Ebadarg);
		return n;
	}
	d = usbdevice(c);
	t = QID(c->qid);
	switch(t){
	case Qctl:
		if(n >= sizeof(cmd)-1)
			n = sizeof(cmd)-1;
		memmove(cmd, a, n);
		cmd[n] = 0;
		nf = getfields(cmd, fields, nelem(fields));
		if(nf > 1 && strcmp(fields[0], "speed") == 0){
			d->ls = strtoul(fields[1], nil, 0) == 0;
		} else if(nf > 4 && strcmp(fields[0], "class") == 0){
			/* class class subclass proto */
			d->class = strtoul(fields[4], nil, 0);
			d->subclass = strtoul(fields[5], nil, 0);
			d->proto = strtoul(fields[6], nil, 0);
		}else if(nf > 2 && strcmp(fields[0], "data") == 0){
			i = strtoul(fields[1], nil, 0);
			if(i < 0 || i >= nelem(d->ep) || d->ep[i] == nil)
				error(Ebadarg);
			e = d->ep[i];
			e->data01 = strtoul(fields[2], nil, 0) != 0;
		}else if(nf > 2 && strcmp(fields[0], "maxpkt") == 0){
			i = strtoul(fields[1], nil, 0);
			if(i < 0 || i >= nelem(d->ep) || d->ep[i] == nil)
				error(Ebadarg);
			e = d->ep[i];
			e->maxpkt = strtoul(fields[2], nil, 0);
			if(e->maxpkt > 1500)
				e->maxpkt = 1500;
		}else if(nf > 2 && strcmp(fields[0], "debug") == 0){
			i = strtoul(fields[1], nil, 0);
			if(i < -1 || i >= nelem(d->ep) || d->ep[i] == nil)
				error(Ebadarg);
			if (i == -1)
				debug = 0;
			else {
				debug = 1;
				e = d->ep[i];
				e->debug = strtoul(fields[2], nil, 0);
			}
		}else if(nf > 1 && strcmp(fields[0], "unstall") == 0){
			i = strtoul(fields[1], nil, 0);
			if(i < 0 || i >= nelem(d->ep) || d->ep[i] == nil)
				error(Ebadarg);
			e = d->ep[i];
			e->err = nil;
		}else if(nf == 6 && strcmp(fields[0], "ep") == 0){
			/* ep n maxpkt mode poll nbuf */
			i = strtoul(fields[1], nil, 0);
			if(i < 0 || i >= nelem(d->ep)) {
				XPRINT("field 1: 0 <= %d < %d\n", i, nelem(d->ep));
				error(Ebadarg);
			}
			if(d->ep[i] != nil)
				error(Einuse);
			e = devendpt(d, i, 1);
			if(waserror()){
				freept(e);
				nexterror();
			}
			i = strtoul(fields[2], nil, 0);
			if(i < 8 || i > 1023)
				i = 8;
			e->maxpkt = i;
			e->mode = strcmp(fields[3],"r")==0? OREAD: strcmp(fields[3],"w") == 0? OWRITE: ORDWR;
			e->periodic = 0;
			e->sched = -1;
			if(strcmp(fields[4], "bulk") != 0){
				e->periodic = 1;
				i = strtoul(fields[4], nil, 0);
				if(i > 0 && i <= 1000)
					e->pollms = i;
				else {
					XPRINT("field 4: 0 <= %d <= 1000\n", i);
					error(Ebadarg);
				}
			}
			i = strtoul(fields[5], nil, 0);
			if(i >= 1 && i <= 32)
				e->nbuf = i;
			poperror();
		}else {
			XPRINT("command %s, fields %d\n", fields[0], nf);
			error(Ebadarg);
		}
		return n;

	case Qsetup:	/* SETUP endpoint 0 */
		/* should canqlock etc */
		if((e = d->ep[0]) == nil)
			error(Eio);	/* can't happen */
		if(n < 8 || n > 1023)
			error(Eio);
		nw = *(uchar*)a & RD2H;
		e->data01 = 0;
		n = writeusb(e, a, n, TokSETUP);
		if(nw == 0){	/* host to device: use IN[DATA1] to ack */
			e->data01 = 1;
			nw = readusb(e, cmd, 8);
			if(nw != 0)
				error(Eio);	/* could provide more status */
		}else
			e->setin = 1;	/* two-phase */
		break;

	default:	/* sends DATA[01] */
		if((t -= Qep0) < 0 || t >= nelem(d->ep))
			error(Eio);
		if((e = d->ep[t]) == nil || e->mode == OREAD)
			error(Eio);	/* can't happen */
		n = writeusb(e, a, n, TokOUT);
		break;
	}
	return n;
}

Dev usbdevtab = {
	'U',
	"usb",

	usbreset,
	usbinit,
	usbattach,
	usbclone,
	usbwalk,
	usbstat,
	usbopen,
	devcreate,
	usbclose,
	usbread,
	devbread,
	usbwrite,
	devbwrite,
	devremove,
	devwstat,
};