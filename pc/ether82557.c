/*
 * Intel 82557 Fast Ethernet PCI Bus LAN Controller
 * as found on the Intel EtherExpress PRO/100B. This chip is full
 * of smarts, unfortunately they're not all in the right place.
 * To do:
 *	the PCI scanning code could be made common to other adapters;
 *	auto-negotiation, full-duplex;
 *	optionally use memory-mapped registers;
 *	detach for PCI reset problems (also towards loadable drivers).
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"
#include "../port/netif.h"

#include "etherif.h"

enum {
	Nrfd		= 64,		/* receive frame area */
	Ncb		= 64,		/* maximum control blocks queued */

	NullPointer	= 0xFFFFFFFF,	/* 82557 NULL pointer */
};

enum {					/* CSR */
	Status		= 0x00,		/* byte or word (word includes Ack) */
	Ack		= 0x01,		/* byte */
	CommandR	= 0x02,		/* byte or word (word includes Interrupt) */
	Interrupt	= 0x03,		/* byte */
	General		= 0x04,		/* dword */
	Port		= 0x08,		/* dword */
	Fcr		= 0x0C,		/* Flash control register */
	Ecr		= 0x0E,		/* EEPROM control register */
	Mcr		= 0x10,		/* MDI control register */
};

enum {					/* Status */
	RUidle		= 0x0000,
	RUsuspended	= 0x0004,
	RUnoresources	= 0x0008,
	RUready		= 0x0010,
	RUrbd		= 0x0020,	/* bit */
	RUstatus	= 0x003F,	/* mask */

	CUidle		= 0x0000,
	CUsuspended	= 0x0040,
	CUactive	= 0x0080,
	CUstatus	= 0x00C0,	/* mask */

	StatSWI		= 0x0400,	/* SoftWare generated Interrupt */
	StatMDI		= 0x0800,	/* MDI r/w done */
	StatRNR		= 0x1000,	/* Receive unit Not Ready */
	StatCNA		= 0x2000,	/* Command unit Not Active (Active->Idle) */
	StatFR		= 0x4000,	/* Finished Receiving */
	StatCX		= 0x8000,	/* Command eXecuted */
	StatTNO		= 0x8000,	/* Transmit NOT OK */
};

enum {					/* Command (byte) */
	CUnop		= 0x00,
	CUstart		= 0x10,
	CUresume	= 0x20,
	LoadDCA		= 0x40,		/* Load Dump Counters Address */
	DumpSC		= 0x50,		/* Dump Statistical Counters */
	LoadCUB		= 0x60,		/* Load CU Base */
	ResetSA		= 0x70,		/* Dump and Reset Statistical Counters */

	RUstart		= 0x01,
	RUresume	= 0x02,
	RUabort		= 0x04,
	LoadHDS		= 0x05,		/* Load Header Data Size */
	LoadRUB		= 0x06,		/* Load RU Base */
	RBDresume	= 0x07,		/* Resume frame reception */
};

enum {					/* Interrupt (byte) */
	InterruptM	= 0x01,		/* interrupt Mask */
	InterruptSI	= 0x02,		/* Software generated Interrupt */
};

enum {					/* Ecr */
	EEsk		= 0x01,		/* serial clock */
	EEcs		= 0x02,		/* chip select */
	EEdi		= 0x04,		/* serial data in */
	EEdo		= 0x08,		/* serial data out */

	EEstart		= 0x04,		/* start bit */
	EEread		= 0x02,		/* read opcode */

	EEaddrsz	= 6,		/* bits of address */
};

enum {					/* Mcr */
	MDIread		= 0x08000000,	/* read opcode */
	MDIwrite	= 0x04000000,	/* write opcode */
	MDIready	= 0x10000000,	/* ready bit */
	MDIie		= 0x20000000,	/* interrupt enable */
};

typedef struct Rfd {
	int	field;
	ulong	link;
	ulong	rbd;
	ushort	count;
	ushort	size;

	uchar	data[sizeof(Etherpkt)];
} Rfd;

enum {					/* field */
	RfdCollision	= 0x00000001,
	RfdIA		= 0x00000002,	/* IA match */
	RfdRxerr	= 0x00000010,	/* PHY character error */
	RfdType		= 0x00000020,	/* Type frame */
	RfdRunt		= 0x00000080,
	RfdOverrun	= 0x00000100,
	RfdBuffer	= 0x00000200,
	RfdAlignment	= 0x00000400,
	RfdCRC		= 0x00000800,

	RfdOK		= 0x00002000,	/* frame received OK */
	RfdC		= 0x00008000,	/* reception Complete */
	RfdSF		= 0x00080000,	/* Simplified or Flexible (1) Rfd */
	RfdH		= 0x00100000,	/* Header RFD */

	RfdI		= 0x20000000,	/* Interrupt after completion */
	RfdS		= 0x40000000,	/* Suspend after completion */
	RfdEL		= 0x80000000,	/* End of List */
};

enum {					/* count */
	RfdF		= 0x4000,
	RfdEOF		= 0x8000,
};

typedef struct Cb Cb;
typedef struct Cb {
	ushort	status;
	ushort	command;
	ulong	link;
	union {
		uchar	data[24];	/* CbIAS + CbConfigure */
		struct {
			ulong	tbd;
			ushort	count;
			uchar	threshold;
			uchar	number;

			ulong	tba;
			ushort	tbasz;
			ushort	pad;
		};
	};

	Block*	bp;
	Cb*	next;
} Cb;

enum {					/* action command */
	CbU		= 0x1000,	/* transmit underrun */
	CbOK		= 0x2000,	/* DMA completed OK */
	CbC		= 0x8000,	/* execution Complete */

	CbNOP		= 0x0000,
	CbIAS		= 0x0001,	/* Individual Address Setup */
	CbConfigure	= 0x0002,
	CbMAS		= 0x0003,	/* Multicast Address Setup */
	CbTransmit	= 0x0004,
	CbDump		= 0x0006,
	CbDiagnose	= 0x0007,
	CbCommand	= 0x0007,	/* mask */

	CbSF		= 0x0008,	/* Flexible-mode CbTransmit */

	CbI		= 0x2000,	/* Interrupt after completion */
	CbS		= 0x4000,	/* Suspend after completion */
	CbEL		= 0x8000,	/* End of List */
};

enum {					/* CbTransmit count */
	CbEOF		= 0x8000,
};

typedef struct Ctlr {
	Lock	slock;			/* attach */
	int	state;

	int	port;

	Lock	rlock;			/* registers */
	int	command;		/* last command issued */

	Block*	rfdhead;		/* receive side */
	Block*	rfdtail;
	int	nrfd;

	Lock	cblock;			/* transmit side */
	int	action;
	uchar	configdata[24];
	int	threshold;
	int	ncb;
	Cb*	cbr;
	Cb*	cbhead;
	Cb*	cbtail;
	int	cbq;
	int	cbqmax;

	Lock	dlock;			/* dump statistical counters */
	ulong	dump[17];
} Ctlr;

static uchar configdata[24] = {
	0x16,				/* byte count */
	0x08,				/* Rx/Tx FIFO limit */
	0x00,				/* adaptive IFS */
	0x00,	
	0x00,				/* Rx DMA maximum byte count */
	0x80,				/* Tx DMA maximum byte count */
	0x32,				/* !late SCB, CNA interrupts */
	0x03,				/* discard short Rx frames */
	0x00,				/* 503/MII */

	0x00,	
	0x2E,				/* normal operation, NSAI */
	0x00,				/* linear priority */
	0x60,				/* inter-frame spacing */
	0x00,	
	0xF2,	
	0xC8,				/* promiscuous mode off */
	0x00,	
	0x40,	
	0xF3,				/* transmit padding enable */
	0x80,				/* full duplex pin enable */
	0x3F,				/* no Multi IA */
	0x05,				/* no Multi Cast ALL */
};

#define csr8r(c, r)	(inb((c)->port+(r)))
#define csr16r(c, r)	(ins((c)->port+(r)))
#define csr32r(c, r)	(inl((c)->port+(r)))
#define csr8w(c, r, b)	(outb((c)->port+(r), (int)(b)))
#define csr16w(c, r, w)	(outs((c)->port+(r), (ushort)(w)))
#define csr32w(c, r, l)	(outl((c)->port+(r), (ulong)(l)))

static void
command(Ctlr* ctlr, int c, int v)
{
	ilock(&ctlr->rlock);

	/*
	 * Only back-to-back CUresume can be done
	 * without waiting for any previous command to complete.
	 * This should be the common case.
	 * Unfortunately there's a chip errata where back-to-back
	 * CUresumes can be lost, the fix is to always wait.
	if(c == CUresume && ctlr->command == CUresume){
		csr8w(ctlr, CommandR, c);
		iunlock(&ctlr->rlock);
		return;
	}
	 */

	while(csr8r(ctlr, CommandR))
		;

	switch(c){

	case CUstart:
	case LoadDCA:
	case LoadCUB:
	case RUstart:
	case LoadHDS:
	case LoadRUB:
		csr32w(ctlr, General, v);
		break;

	/*
	case CUnop:
	case CUresume:
	case DumpSC:
	case ResetSA:
	case RUresume:
	case RUabort:
	 */
	default:
		break;
	}
	csr8w(ctlr, CommandR, c);
	ctlr->command = c;

	iunlock(&ctlr->rlock);
}

static Block*
rfdalloc(ulong link)
{
	Block *bp;
	Rfd *rfd;

	if(bp = iallocb(sizeof(Rfd))){
		rfd = (Rfd*)bp->rp;
		rfd->field = 0;
		rfd->link = link;
		rfd->rbd = NullPointer;
		rfd->count = 0;
		rfd->size = sizeof(Etherpkt);
	}

	return bp;
}

static void
attach(Ether* ether)
{
	Ctlr *ctlr;

	ctlr = ether->ctlr;
	lock(&ctlr->slock);
	if(ctlr->state == 0){
		command(ctlr, RUstart, PADDR(ctlr->rfdhead->rp));
		ctlr->state = 1;
	}
	unlock(&ctlr->slock);
}

static long
ifstat(Ether* ether, void* a, long n, ulong offset)
{
	char *p;
	int len;
	Ctlr *ctlr;
	ulong dump[17];

	ctlr = ether->ctlr;
	lock(&ctlr->dlock);

	/*
	 * Start the command then
	 * wait for completion status,
	 * should be 0xA005.
	 */
	ctlr->dump[16] = 0;
	command(ctlr, DumpSC, 0);
	while(ctlr->dump[16] == 0)
		;

	ether->oerrs = ctlr->dump[1]+ctlr->dump[2]+ctlr->dump[3];
	ether->crcs = ctlr->dump[10];
	ether->frames = ctlr->dump[11];
	ether->buffs = ctlr->dump[12]+ctlr->dump[15];
	ether->overflows = ctlr->dump[13];

	if(n == 0){
		unlock(&ctlr->dlock);
		return 0;
	}

	memmove(dump, ctlr->dump, sizeof(dump));
	unlock(&ctlr->dlock);

	p = malloc(READSTR);
	len = snprint(p, READSTR, "transmit good frames: %lud\n", dump[0]);
	len += snprint(p+len, READSTR-len, "transmit maximum collisions errors: %lud\n", dump[1]);
	len += snprint(p+len, READSTR-len, "transmit late collisions errors: %lud\n", dump[2]);
	len += snprint(p+len, READSTR-len, "transmit underrun errors: %lud\n", dump[3]);
	len += snprint(p+len, READSTR-len, "transmit lost carrier sense: %lud\n", dump[4]);
	len += snprint(p+len, READSTR-len, "transmit deferred: %lud\n", dump[5]);
	len += snprint(p+len, READSTR-len, "transmit single collisions: %lud\n", dump[6]);
	len += snprint(p+len, READSTR-len, "transmit multiple collisions: %lud\n", dump[7]);
	len += snprint(p+len, READSTR-len, "transmit total collisions: %lud\n", dump[8]);
	len += snprint(p+len, READSTR-len, "receive good frames: %lud\n", dump[9]);
	len += snprint(p+len, READSTR-len, "receive CRC errors: %lud\n", dump[10]);
	len += snprint(p+len, READSTR-len, "receive alignment errors: %lud\n", dump[11]);
	len += snprint(p+len, READSTR-len, "receive resource errors: %lud\n", dump[12]);
	len += snprint(p+len, READSTR-len, "receive overrun errors: %lud\n", dump[13]);
	len += snprint(p+len, READSTR-len, "receive collision detect errors: %lud\n", dump[14]);
	len += snprint(p+len, READSTR-len, "receive short frame errors: %lud\n", dump[15]);
	snprint(p+len, READSTR-len, "cbqmax: %lud\n", ctlr->cbqmax);
ctlr->cbqmax = 0;

	n = readstr(offset, a, n, p);
	free(p);

	return n;
}

static void
txstart(Ether* ether)
{
	Ctlr *ctlr;
	Block *bp;
	Cb *cb;

	ctlr = ether->ctlr;
	while(ctlr->cbq < (ctlr->ncb-1)){
		cb = ctlr->cbhead->next;
		if(ctlr->action == 0){
			bp = qget(ether->oq);
			if(bp == nil)
				break;

			cb->command = CbS|CbSF|CbTransmit;
			cb->tbd = PADDR(&cb->tba);
			cb->count = 0;
			cb->threshold = ctlr->threshold;
			cb->number = 1;
			cb->tba = PADDR(bp->rp);
			cb->bp = bp;
			cb->tbasz = BLEN(bp);
		}
		else if(ctlr->action == CbConfigure){
			cb->command = CbS|CbConfigure;
			memmove(cb->data, ctlr->configdata, sizeof(ctlr->configdata));
			ctlr->action = 0;
		}
		else if(ctlr->action == CbIAS){
			cb->command = CbS|CbIAS;
			memmove(cb->data, ether->ea, Eaddrlen);
			ctlr->action = 0;
		}
		else{
			print("#l%d: action 0x%uX\n", ether->ctlrno, ctlr->action);
			ctlr->action = 0;
			break;
		}
		cb->status = 0;

		ctlr->cbhead->command &= ~CbS;
		ctlr->cbhead = cb;
		ctlr->cbq++;
	}
	command(ctlr, CUresume, 0);

	if(ctlr->cbq > ctlr->cbqmax)
		ctlr->cbqmax = ctlr->cbq;
}

static void
configure(Ether* ether, int promiscuous)
{
	Ctlr *ctlr;

	ctlr = ether->ctlr;
	ilock(&ctlr->cblock);
	if(promiscuous){
		ctlr->configdata[6] |= 0x80;		/* Save Bad Frames */
		ctlr->configdata[6] &= ~0x40;		/* !Discard Overrun Rx Frames */
		ctlr->configdata[7] &= ~0x01;		/* !Discard Short Rx Frames */
		ctlr->configdata[15] |= 0x01;		/* Promiscuous mode */
		ctlr->configdata[18] &= ~0x01;		/* (!Padding enable?), !stripping enable */
		ctlr->configdata[21] |= 0x08;		/* Multi Cast ALL */
	}
	else{
		ctlr->configdata[6] &= ~0x80;
		ctlr->configdata[7] |= 0x01;
		ctlr->configdata[15] &= ~0x01;
		ctlr->configdata[18] |= 0x01;		/* 0x03? */
		ctlr->configdata[21] &= ~0x08;
	}
	ctlr->action = CbConfigure;
	txstart(ether);
	iunlock(&ctlr->cblock);
}

static void
promiscuous(void* arg, int on)
{
	configure(arg, on);
}

static void
transmit(Ether* ether)
{
	Ctlr *ctlr;

	ctlr = ether->ctlr;
	ilock(&ctlr->cblock);
	txstart(ether);
	iunlock(&ctlr->cblock);
}

static void
interrupt(Ureg*, void* arg)
{
	Rfd *rfd;
	Cb* cb;
	Block *bp, *xbp;
	Ctlr *ctlr;
	Ether *ether;
	int status;

	ether = arg;
	ctlr = ether->ctlr;

	for(;;){
		lock(&ctlr->rlock);
		status = csr16r(ctlr, Status);
		csr8w(ctlr, Ack, (status>>8) & 0xFF);
		unlock(&ctlr->rlock);

		if(!(status & (StatCX|StatFR|StatCNA|StatRNR|StatMDI|StatSWI)))
			break;

		if(status & StatFR){
			bp = ctlr->rfdhead;
			rfd = (Rfd*)bp->rp;
			while(rfd->field & RfdC){
				/*
				 * If it's an OK receive frame and a replacement buffer
				 * can be allocated then
				 *	adjust the received buffer pointers for the
				 *	  actual data received;
				 *	initialise the replacement buffer to point to
				 *	  the next in the ring;
				 *	pass the received buffer on for disposal;
				 *	initialise bp to point to the replacement.
				 * If not, just adjust the necessary fields for reuse.
				 */
				if((rfd->field & RfdOK) && (xbp = rfdalloc(rfd->link))){
					bp->rp += sizeof(Rfd)-sizeof(Etherpkt);
					bp->wp = bp->rp + (rfd->count & 0x3FFF);

					xbp->next = bp->next;
					bp->next = 0;

					etheriq(ether, bp, 1);
					bp = xbp;
				}
				else{
					rfd->field = 0;
					rfd->count = 0;
				}

				/*
				 * The ring tail pointer follows the head with with one
				 * unused buffer in between to defeat hardware prefetch;
				 * once the tail pointer has been bumped on to the next
				 * and the new tail has the Suspend bit set, it can be
				 * removed from the old tail buffer.
				 * As a replacement for the current head buffer may have
				 * been allocated above, ensure that the new tail points
				 * to it (next and link).
				 */
				rfd = (Rfd*)ctlr->rfdtail->rp;
				ctlr->rfdtail = ctlr->rfdtail->next;
				ctlr->rfdtail->next = bp;
				((Rfd*)ctlr->rfdtail->rp)->link = PADDR(bp->rp);
				((Rfd*)ctlr->rfdtail->rp)->field |= RfdS;
				rfd->field &= ~RfdS;

				/*
				 * Finally done with the current (possibly replaced)
				 * head, move on to the next and maintain the sentinel
				 * between tail and head.
				 */
				ctlr->rfdhead = bp->next;
				bp = ctlr->rfdhead;
				rfd = (Rfd*)bp->rp;
			}
			status &= ~StatFR;
		}

		if(status & StatRNR){
			command(ctlr, RUresume, 0);
			status &= ~StatRNR;
		}

		if(status & StatCNA){
			lock(&ctlr->cblock);

			cb = ctlr->cbtail;
			while(ctlr->cbq){
				if(!(cb->status & CbC))
					break;
				if(cb->bp){
					freeb(cb->bp);
					cb->bp = nil;
				}
				if((cb->status & CbU) && ctlr->threshold < 0xE0)
					ctlr->threshold++;

				ctlr->cbq--;
				cb = cb->next;
			}
			ctlr->cbtail = cb;

			txstart(ether);
			unlock(&ctlr->cblock);

			status &= ~StatCNA;
		}

		if(status & (StatCX|StatFR|StatCNA|StatRNR|StatMDI|StatSWI))
			panic("#l%d: status %uX\n", ether->ctlrno, status);
	}
}

static void
ctlrinit(Ctlr* ctlr)
{
	int i;
	Block *bp;
	Rfd *rfd;
	ulong link;

	/*
	 * Create the Receive Frame Area (RFA) as a ring of allocated
	 * buffers.
	 * A sentinel buffer is maintained between the last buffer in
	 * the ring (marked with RfdS) and the head buffer to defeat the
	 * hardware prefetch of the next RFD and allow dynamic buffer
	 * allocation.
	 */
	link = NullPointer;
	for(i = 0; i < Nrfd; i++){
		bp = rfdalloc(link);
		if(ctlr->rfdhead == nil)
			ctlr->rfdtail = bp;
		bp->next = ctlr->rfdhead;
		ctlr->rfdhead = bp;
		link = PADDR(bp->rp);
	}
	ctlr->rfdtail->next = ctlr->rfdhead;
	rfd = (Rfd*)ctlr->rfdtail->rp;
	rfd->link = PADDR(ctlr->rfdhead->rp);
	rfd->field |= RfdS;
	ctlr->rfdhead = ctlr->rfdhead->next;

	/*
	 * Create a ring of control blocks for the
	 * transmit side.
	 */
	ilock(&ctlr->cblock);
	ctlr->cbr = malloc(ctlr->ncb*sizeof(Cb));
	for(i = 0; i < ctlr->ncb; i++){
		ctlr->cbr[i].status = CbC|CbOK;
		ctlr->cbr[i].command = CbS|CbNOP;
		ctlr->cbr[i].link = PADDR(&ctlr->cbr[NEXT(i, ctlr->ncb)].status);
		ctlr->cbr[i].next = &ctlr->cbr[NEXT(i, ctlr->ncb)];
	}
	ctlr->cbhead = ctlr->cbr;
	ctlr->cbtail = ctlr->cbr;
	ctlr->cbq = 0;

	memmove(ctlr->configdata, configdata, sizeof(configdata));
	ctlr->threshold = 8;

	iunlock(&ctlr->cblock);
}

static int
dp83840r(Ctlr* ctlr, int phyadd, int regadd)
{
	int mcr, timo;

	/*
	 * DP83840
	 * 10/100Mb/s Ethernet Physical Layer.
	 */
	csr32w(ctlr, Mcr, MDIread|(phyadd<<21)|(regadd<<16));
	mcr = 0;
	for(timo = 10; timo; timo--){
		mcr = csr32r(ctlr, Mcr);
		if(mcr & MDIready)
			break;
		delay(1);
	}

	if(mcr & MDIready)
		return mcr & 0xFFFF;

	return -1;
}

static int
hy93c46r(Ctlr* ctlr, int r)
{
	int i, op, data;

	/*
	 * Hyundai HY93C46 or equivalent serial EEPROM.
	 * This sequence for reading a 16-bit register 'r'
	 * in the EEPROM is taken straight from Section
	 * 2.3.4.2 of the Intel 82557 User's Guide.
	 */
	csr16w(ctlr, Ecr, EEcs);
	op = EEstart|EEread;
	for(i = 2; i >= 0; i--){
		data = (((op>>i) & 0x01)<<2)|EEcs;
		csr16w(ctlr, Ecr, data);
		csr16w(ctlr, Ecr, data|EEsk);
		delay(1);
		csr16w(ctlr, Ecr, data);
		delay(1);
	}

	for(i = EEaddrsz-1; i >= 0; i--){
		data = (((r>>i) & 0x01)<<2)|EEcs;
		csr16w(ctlr, Ecr, data);
		csr16w(ctlr, Ecr, data|EEsk);
		delay(1);
		csr16w(ctlr, Ecr, data);
		delay(1);
		if(!(csr16r(ctlr, Ecr) & EEdo))
			break;
	}

	data = 0;
	for(i = 15; i >= 0; i--){
		csr16w(ctlr, Ecr, EEcs|EEsk);
		delay(1);
		if(csr16r(ctlr, Ecr) & EEdo)
			data |= (1<<i);
		csr16w(ctlr, Ecr, EEcs);
		delay(1);
	}

	csr16w(ctlr, Ecr, 0);

	return data;
}

typedef struct Adapter {
	int	port;
	int	irq;
	int	tbdf;
} Adapter;
static Block* adapter;

static void
i82557adapter(Block** bpp, int port, int irq, int tbdf)
{
	Block *bp;
	Adapter *ap;

	bp = allocb(sizeof(Adapter));
	ap = (Adapter*)bp->rp;
	ap->port = port;
	ap->irq = irq;
	ap->tbdf = tbdf;

	bp->next = *bpp;
	*bpp = bp;
}

static void
i82557pci(void)
{
	Pcidev *p;

	p = nil;
	while(p = pcimatch(p, 0x8086, 0x1229)){
		/*
		 * bar[0] is the memory-mapped register address (4KB),
		 * bar[1] is the I/O port register address (32 bytes) and
		 * bar[2] is for the flash ROM (1MB).
		 */
		i82557adapter(&adapter, p->mem[1].bar & ~0x01, p->intl, p->tbdf);
	}
}

static int
reset(Ether* ether)
{
	int i, port, x;
	Block *bp, **bpp;
	Adapter *ap;
	uchar ea[Eaddrlen];
	Ctlr *ctlr;
	static int scandone;

	if(scandone == 0){
		i82557pci();
		scandone = 1;
	}

	/*
	 * Any adapter matches if no port is supplied,
	 * otherwise the ports must match.
	 */
	port = 0;
	bpp = &adapter;
	for(bp = *bpp; bp; bp = bp->next){
		ap = (Adapter*)bp->rp;
		if(ether->port == 0 || ether->port == ap->port){
			port = ap->port;
			ether->irq = ap->irq;
			ether->tbdf = ap->tbdf;
			*bpp = bp->next;
			freeb(bp);
			break;
		}
		bpp = &bp->next;
	}
	if(port == 0)
		return -1;

	/*
	 * Allocate a controller structure and start to initialise it.
	 * Perform a software reset after which should ensure busmastering
	 * is still enabled. The EtherExpress PRO/100B appears to leave
	 * the PCI configuration alone (see the 'To do' list above) so punt
	 * for now.
	 * Load the RUB and CUB registers for linear addressing (0).
	 */
	ether->ctlr = malloc(sizeof(Ctlr));
	ctlr = ether->ctlr;
	ctlr->port = port;

	ilock(&ctlr->rlock);
	csr32w(ctlr, Port, 0);
	delay(1);
	iunlock(&ctlr->rlock);

	command(ctlr, LoadRUB, 0);
	command(ctlr, LoadCUB, 0);
	command(ctlr, LoadDCA, PADDR(ctlr->dump));

	/*
	 * Initialise the receive frame, transmit ring and configuration areas.
	 */
	ctlr->ncb = Ncb;
	ctlrinit(ctlr);

	/*
	 * Possibly need to configure the physical-layer chip here, but the
	 * EtherExpress PRO/100B appears to bring it up with a sensible default
	 * configuration. However, should check for the existence of the PHY
	 * and, if found, check whether to use 82503 (serial) or MII (nibble)
	 * mode. Verify the PHY is a National Semiconductor DP83840 (OUI 0x80017)
	 * or an Intel 82555 (OUI 0xAA00) by looking at the Organizationally Unique
	 * Identifier (OUI) in registers 2 and 3.
	 */
	for(i = 1; i < 32; i++){
		if((x = dp83840r(ctlr, i, 2)) == 0xFFFF)
			continue;
		x <<= 6;
		x |= dp83840r(ctlr, i, 3)>>10;
		if(x != 0x80017 && x != 0xAA00)
			print("#l%d: unrecognised PHY - OUI 0x%4.4uX\n", ether->ctlrno, x);

		x = dp83840r(ctlr, i, 0x19);
		if(!(x & 0x0040)){
			ether->mbps = 100;
			ctlr->configdata[8] = 1;
			ctlr->configdata[15] &= ~0x80;
		}
		else{
			x = dp83840r(ctlr, i, 0x1B);
			if(!(x & 0x0200)){
				ctlr->configdata[8] = 1;
				ctlr->configdata[15] &= ~0x80;
			}
		}
		break;
	}

	/*
	 * Load the chip configuration and start it off.
	 */
	if(ether->oq == 0)
		ether->oq = qopen(256*1024, 1, 0, 0);
	configure(ether, 0);
	command(ctlr, CUstart, PADDR(&ctlr->cbr->status));

	/*
	 * Check if the adapter's station address is to be overridden.
	 * If not, read it from the EEPROM and set in ether->ea prior to loading
	 * the station address with the Individual Address Setup command.
	 */
	memset(ea, 0, Eaddrlen);
	if(memcmp(ea, ether->ea, Eaddrlen) == 0){
		for(i = 0; i < Eaddrlen/2; i++){
			x = hy93c46r(ctlr, i);
			ether->ea[2*i] = x;
			ether->ea[2*i+1] = x>>8;
		}
	}

	ilock(&ctlr->cblock);
	ctlr->action = CbIAS;
	txstart(ether);
	iunlock(&ctlr->cblock);

	/*
	 * Linkage to the generic ethernet driver.
	 */
	ether->port = port;
	ether->attach = attach;
	ether->transmit = transmit;
	ether->interrupt = interrupt;
	ether->ifstat = ifstat;

	ether->promiscuous = promiscuous;
	ether->arg = ether;

	return 0;
}

void
ether82557link(void)
{
	addethercard("i82557",  reset);
}
