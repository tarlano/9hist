/*
 * PCI support code.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

enum
{					/* configuration mechanism #1 */
	PciADDR		= 0xCF8,	/* CONFIG_ADDRESS */
	PciDATA		= 0xCFC,	/* CONFIG_DATA */

					/* configuration mechanism #2 */
	PciCSE		= 0xCF8,	/* configuration space enable */
	PciFORWARD	= 0xCFA,	/* which bus */

	MaxFNO		= 7,
	MaxUBN		= 255,

	BARio		= 0,		/* fake BAR registers for bridges */
	BARmem,

	NOBIOS		= 0,		/* initialise if the BIOS didn't */
};

enum {					/* command register */
	IOen		= (1<<0),
	MEMen		= (1<<1),
	MASen		= (1<<2),
	MemWrInv	= (1<<4),
	PErrEn		= (1<<6),
	SErrEn		= (1<<8),
};

static Lock pcicfglock;
static Lock pcicfginitlock;
static int pcicfgmode = -1;
static int pcimaxdno;
static Pcidev* pciroot;
static Pcidev* pcilist;
static Pcidev* pcitail;

static int pcicfgrw32(int, int, int, int);
static int pcicfgrw8(int, int, int, int);

ulong
pcibarsize(Pcidev *p, int rno)
{
	ulong v, size;

	v = pcicfgrw32(p->tbdf, rno, 0, 1);
	pcicfgrw32(p->tbdf, rno, 0xFFFFFFF0, 0);
	size = pcicfgrw32(p->tbdf, rno, 0, 1);
	if(v & 1)
		size |= 0xFFFF0000;
	pcicfgrw32(p->tbdf, rno, v, 0);

	return -(size & ~0x0F);
}

static void
pcibusmap(Pcidev *p, ulong *pmema, ulong *pioa, int wrreg)
{
	int i, size, k;
	ulong addr, v, mema, ioa, pcr, tmema, tioa;

	if(!NOBIOS)
		return;

	mema = *pmema;
	ioa = *pioa;

	print("pcibusmap wr=%d %d.%d.%d mem=%lux io=%lux\n", 
		wrreg, BUSBNO(p->tbdf), BUSDNO(p->tbdf), BUSFNO(p->tbdf),
		mema, ioa);

	/*
	 * Allocate address space on this bus
	 * note this loop follows link, not list
	 */
	for(; p; p = p->link) {
		if(p->ccrb == 0x06) {
			if(p->ccru != 0x04) {
				print("pci: ignored bridge %d.%d.%d\n",
					BUSBNO(p->tbdf),
					BUSDNO(p->tbdf),
					BUSFNO(p->tbdf));
				continue;
			}

			/* Base */
			p->mem[BARio].bar = ioa;
			p->mem[BARmem].bar = mema;
			tioa = ioa;
			tmema = mema;

			/* Limit */
			ioa += p->mem[BARio].size;
			mema += p->mem[BARmem].size;

			if(wrreg == 0)
				continue;

			pcibusmap(p->bridge, &tmema, &tioa, 1);

			/*
			 * IO base[15:12], IO limit [15:12]
			 */
			v =	(0xFFFF<<16)|
				(ioa & 0xF000)|
				((p->mem[BARio].bar & 0xF000)>>8);

			pcicfgrw32(p->tbdf, PciBAR3, v, 0);

			/*
			 * Memory Base/Limit
			 */
			v =	(mema & ~((1<<20)-1)) |
				((p->mem[BARmem].bar & ~((1<<20)-1)) >> 16);

			pcicfgrw32(p->tbdf, PciBAR4, v, 0);

			/*
			 * Disable memory prefetch
			 */
			pcicfgrw32(p->tbdf, PciBAR5, 0x0000FFFF, 0);

			/*
			 * Enable the bridge
			 */
			v = 0xFFFF0000 | IOen | MEMen | MASen;
			pcicfgrw32(p->tbdf, PciPCR, v, 0);
			continue;
		}

		pcr = pcicfgrw32(p->tbdf, PciPCR, 0, 1);
		
		for(i = PciBAR0; i <= PciBAR5; i += 4) {
			v = pcicfgrw32(p->tbdf, i, 0, 1);
			size = pcibarsize(p, i);
			if(size > 16*1024*1024)
				print("pcimap: size %d?\n", size);
			if(size == 0)
				continue;

			if(v & 1) {		/* Allocate IO space */
				ioa = (ioa+size-1) & ~(size-1);
				addr = ioa;
				ioa += size;
				pcr |= IOen;
			}
			else {			/* Allocate Memory space */
				mema = (mema+size-1) & ~(size-1);
				addr = mema;
				mema += size;
				pcr |= MEMen;
			}

			k = (i-PciBAR0)/4;
			p->mem[k].size = size;
			p->mem[k].bar = addr;
			if(wrreg)
				pcicfgrw32(p->tbdf, i, addr, 0);
		}

		/*
		 * Set latency timer
		 * Enable memory/io/master
		 */
		if(wrreg) {
			pcicfgrw8(p->tbdf, PciLTR, 64, 0);

			pcr |= MASen;
			pcicfgrw32(p->tbdf, PciPCR, pcr, 0);
		}
	}

	print("pcibusmap mem=%lux io=%lux\n", mema, ioa);

	*pmema = mema;
	*pioa = ioa;
}

static int
pciscan(int bno, Pcidev** list)
{
	Pcidev *p, *head, *tail;
	int dno, fno, i, hdt, l, maxfno, maxubn, rno, sbn, tbdf, ubn;

	maxubn = bno;
	head = nil;
	tail = nil;
	for(dno = 0; dno <= pcimaxdno; dno++){
		maxfno = 0;
		for(fno = 0; fno <= maxfno; fno++){
			/*
			 * For this possible device, form the
			 * bus+device+function triplet needed to address it
			 * and try to read the vendor and device ID.
			 * If successful, allocate a device struct and
			 * start to fill it in with some useful information
			 * from the device's configuration space.
			 */
			tbdf = MKBUS(BusPCI, bno, dno, fno);
			l = pcicfgrw32(tbdf, PciVID, 0, 1);
			if(l == 0xFFFFFFFF || l == 0)
				continue;
			p = malloc(sizeof(*p));
			p->tbdf = tbdf;
			p->vid = l;
			p->did = l>>16;

			if(pcilist != nil)
				pcitail->list = p;
			else
				pcilist = p;
			pcitail = p;

			p->intl = pcicfgr8(p, PciINTL);
			p->ccrp = pcicfgr8(p, PciCCRp);
			p->ccru = pcicfgr8(p, PciCCRu);
			p->ccrb = pcicfgr8(p, PciCCRb);

			/*
			 * If the device is a multi-function device adjust the
			 * loop count so all possible functions are checked.
			 */
			hdt = pcicfgr8(p, PciHDT);
			if(hdt & 0x80)
				maxfno = MaxFNO;

			/*
			 * If appropriate, read the base address registers
			 * and work out the sizes.
			 */
			switch(p->ccrb) {
			case 0x01:		/* mass storage controller */
			case 0x02:		/* network controller */
			case 0x03:		/* display controller */
			case 0x04:		/* multimedia device */
			case 0x07:		/* simple comm. controllers */
			case 0x08:		/* base system peripherals */
			case 0x09:		/* input devices */
			case 0x0A:		/* docking stations */
			case 0x0B:		/* processors */
			case 0x0C:		/* serial bus controllers */
				if((hdt & 0x7F) != 0)
					break;
				rno = PciBAR0 - 4;
				for(i = 0; i < nelem(p->mem); i++) {
					rno += 4;
					p->mem[i].bar = pcicfgr32(p, rno);
					p->mem[i].size = pcibarsize(p, rno);
				}
				break;

			case 0x00:
			case 0x05:		/* memory controller */
			case 0x06:		/* bridge device */
			default:
				break;
			}

			if(head != nil)
				tail->link = p;
			else
				head = p;
			tail = p;
		}
	}

	*list = head;
	for(p = head; p != nil; p = p->link){
		/*
		 * Find PCI-PCI bridges and recursively descend the tree.
		 */
		if(p->ccrb != 0x06 || p->ccru != 0x04)
			continue;

		/*
		 * If the secondary or subordinate bus number is not
		 * initialised try to do what the PCI BIOS should have
		 * done and fill in the numbers as the tree is descended.
		 * On the way down the subordinate bus number is set to
		 * the maximum as it's not known how many buses are behind
		 * this one; the final value is set on the way back up.
		 */
		sbn = pcicfgr8(p, PciSBN);
		ubn = pcicfgr8(p, PciUBN);

		if(sbn == 0 || ubn == 0 || NOBIOS) {
			sbn = maxubn+1;
			/*
			 * Make sure memory, I/O and master enables are
			 * off, set the primary, secondary and subordinate
			 * bus numbers and clear the secondary status before
			 * attempting to scan the secondary bus.
			 *
			 * Initialisation of the bridge should be done here.
			 */
			pcicfgw32(p, PciPCR, 0xFFFF0000);
			l = (MaxUBN<<16)|(sbn<<8)|bno;
			pcicfgw32(p, PciPBN, l);
			pcicfgw16(p, PciSPSR, 0xFFFF);
			maxubn = pciscan(sbn, &p->bridge);
			l = (maxubn<<16)|(sbn<<8)|bno;

			pcicfgw32(p, PciPBN, l);
		}
		else {
			maxubn = ubn;
			pciscan(sbn, &p->bridge);
		}

		/*
		 * Now figure out the size of things below the bridge
		 */
		if(NOBIOS) {
			mema = 0;
			ioa = 0;
			pcibusmap(p->bridge, &mema, &ioa, 0);
			p->mem[BARmem].size = ROUND(mema, 1<<20);
			p->mem[BARio].size = ROUND(ioa, 1<<12);
		}
	}

	return maxubn;
}

static ulong
pcimask(ulong v)
{
	ulong m;

	if(!NOBIOS)
		return 0;

	m = BI2BY*sizeof(v);
	for(m = 1<<(m-1); m != 0; m >>= 1) {
		if(m & v)
			break;
	}
	v |= m-1;
	return v+1;
}

static void
pcicfginit(void)
{
	char *p;
	int bno;
	Pcidev **list;
	ulong mema, ioa, size;

	lock(&pcicfginitlock);
	if(pcicfgmode != -1)
		goto out;

	/*
	 * Try to determine which PCI configuration mode is implemented.
	 * Mode2 uses a byte at 0xCF8 and another at 0xCFA; Mode1 uses
	 * a DWORD at 0xCF8 and another at 0xCFC and will pass through
	 * any non-DWORD accesses as normal I/O cycles. There shouldn't be
	 * a device behind these addresses so if Mode2 accesses fail try
	 * for Mode1 (which is preferred, Mode2 is deprecated).
	 */
	outb(PciCSE, 0);
	if(inb(PciCSE) == 0){
		pcicfgmode = 2;
		pcimaxdno = 15;
	}
	else {
		outl(PciADDR, 0);
		if(inl(PciADDR) == 0){
			pcicfgmode = 1;
			pcimaxdno = 31;
		}
	}
	
	if(pcicfgmode < 0)
		goto out;

	if(p = getconf("*pcimaxdno"))
		pcimaxdno = strtoul(p, 0, 0);

	list = &pciroot;
	for(bno = 0; bno < 256; bno++) {
		bno = pciscan(bno, list);
		while(*list)
			list = &(*list)->link;
	}

	if(NOBIOS){
		/*
		 * Work out how big the top bus is
		 */
		mema = 0;
		ioa = 0;
		pcibusmap(pciroot, &mema, &ioa, 0);
	
		print("Sizes: mem=%lux io=%lux\n", mema, ioa);
	
		/*
		 * Align the windows and map it
		 */
		size = pcimask(mema);
		mema = (0xFE000000 & ~(size-1)) - size;
		size = pcimask(ioa);
		ioa = (0xFE00 & ~(size-1)) - size;
	
		pcibusmap(pciroot, &mema, &ioa, 1);
	
		unlock(&pcicfginitlock);
		pcihinv(nil);

		return;
	}
out:
	unlock(&pcicfginitlock);
}

static int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x03;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inb(PciDATA+o);
		else
			outb(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inb((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outb((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr8(Pcidev* pcidev, int rno)
{
	return pcicfgrw8(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw8(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw8(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	int o, type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		o = rno & 0x02;
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = ins(PciDATA+o);
		else
			outs(PciDATA+o, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = ins((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outs((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr16(Pcidev* pcidev, int rno)
{
	return pcicfgrw16(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw16(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw16(pcidev->tbdf, rno, data, 0);
}

static int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	int type, x;

	if(pcicfgmode == -1)
		pcicfginit();

	if(BUSBNO(tbdf))
		type = 0x01;
	else
		type = 0x00;
	x = -1;
	if(BUSDNO(tbdf) > pcimaxdno)
		return x;

	lock(&pcicfglock);
	switch(pcicfgmode){

	case 1:
		rno &= ~0x03;
		outl(PciADDR, 0x80000000|BUSBDF(tbdf)|rno|type);
		if(read)
			x = inl(PciDATA);
		else
			outl(PciDATA, data);
		outl(PciADDR, 0);
		break;

	case 2:
		outb(PciCSE, 0x80|(BUSFNO(tbdf)<<1));
		outb(PciFORWARD, BUSBNO(tbdf));
		if(read)
			x = inl((0xC000|(BUSDNO(tbdf)<<8)) + rno);
		else
			outl((0xC000|(BUSDNO(tbdf)<<8)) + rno, data);
		outb(PciCSE, 0);
		break;
	}
	unlock(&pcicfglock);

	return x;
}

int
pcicfgr32(Pcidev* pcidev, int rno)
{
	return pcicfgrw32(pcidev->tbdf, rno, 0, 1);
}

void
pcicfgw32(Pcidev* pcidev, int rno, int data)
{
	pcicfgrw32(pcidev->tbdf, rno, data, 0);
}

Pcidev*
pcimatch(Pcidev* prev, int vid, int did)
{
	if(pcicfgmode == -1)
		pcicfginit();

	if(prev == nil)
		prev = pcilist;
	else
		prev = prev->list;

	while(prev != nil){
		if((vid == 0 || prev->vid == vid)
		&& (did == 0 || prev->did == did))
			break;
		prev = prev->list;
	}
	return prev;
}

Pcidev*
pcimatchtbdf(int tbdf)
{
	Pcidev *pcidev;

	if(pcicfgmode == -1)
		pcicfginit();

	for(pcidev = pcilist; pcidev != nil; pcidev = pcidev->list) {
		if(pcidev->tbdf == tbdf)
			break;
	}
	return pcidev;
}

void
pcihinv(Pcidev* p)
{
	int i;
	Pcidev *t;

	if(p == nil) {
		p = pciroot;
		print("bus dev type vid  did intl memory\n");
	}
	for(t = p; t != nil; t = t->link) {
		print("%d  %2d/%d %.2ux %.2ux %.2ux %.4ux %.4ux %2d  ",
			BUSBNO(t->tbdf), BUSDNO(t->tbdf), BUSFNO(t->tbdf),
			t->ccrb, t->ccru, t->ccrp, t->vid, t->did, t->intl);

		for(i = 0; i < nelem(p->mem); i++) {
			if(t->mem[i].size == 0)
				continue;
			print("%d:%.8lux %d ", i,
				t->mem[i].bar, t->mem[i].size);
		}
		print("\n");
	}
	while(p != nil) {
		if(p->bridge != nil)
			pcihinv(p->bridge);
		p = p->link;
	}
}

void
pcireset(void)
{
	Pcidev *p;
	int pcr;

	if(pcicfgmode == -1)
		pcicfginit();

	for(p = pcilist; p != nil; p = p->list){
		pcr = pcicfgr16(p, PciPCR);
		pcr &= ~0x0004;
		pcicfgw16(p, PciPCR, pcr);
	}
}

void
pcisetbme(Pcidev* p)
{
	int pcr;

	pcr = pcicfgr16(p, PciPCR);
	pcr |= 0x0004;
	pcicfgw16(p, PciPCR, pcr);
}
