#include "../port/portfns.h"

#define affinity(x) m
void	arginit(void);
void	clearmmucache(void);
void	clock(Ureg*);
void	clockinit(void);
ulong	confeval(char*);
void	confread(void);
void	confprint(void);
void	confset(char*);
int	conschar(void);
void	dcflush(void*, ulong);
int	duartactive(void);
void	duartenable0(void);
void	duartinit(void);
void	duartintr(void);
void	duartreset(void);
void	duartslave(void);
void	duartspecial(int, int, Queue**, Queue**, int (*)(Queue*, int));
void	duartxmit(int);
void	evenaddr(ulong);
void	faultmips(Ureg*, int, int);
ulong	fcr31(void);
void	firmware(int);
void	flushmmu(void);
#define	flushpage(s)	icflush((void*)(s), BY2PG)
void	fptrap(Ureg*);
void	gettlb(int, ulong*);
ulong	gettlbvirt(int);
void	gotopc(ulong);
ulong	getcallerpc(void*);
void	icflush(void *, ulong);
void	ioboardinit(void);
void	intr(Ureg*);
int	iprint(char*, ...);
void	kbdchar(int);
void	kproftimer(ulong);
void	lanceintr(void);
void	lanceparity(void);
void	lancesetup(Lance*);
void	launchinit(void);
void	launch(int);
int	muxlock(int*, int*);
void	newstart(void);
int	newtlbpid(Proc*);
void	novme(int);
void	online(void);
Block*	prepend(Block*, int);
void	prflush(void);
void	printslave(void);
#define procsetup(p)	((p)->fpstate = FPinit)
#define procsave(p)
#define procrestore(p)
void	purgetlb(int);
void	putcontext(int);
void	putstlb(ulong, ulong);
void	putstrn(char*, long);
void	puttlb(ulong, ulong);
void	puttlbx(int, ulong, ulong);
int	readlog(ulong, char*, ulong);
void	restfpregs(FPsave*, ulong);
#define	screenputs(a, b)
void	scsiintr(int);
void	setvmevec(int, void (*)(int));
void	sinit(void);
uchar*	smap(int, uchar*);
void	sunmap(int, uchar*);
long	syscall(Ureg*);
void	sysloginit(void);
void	syslog(char*, int);
void	tlbinit(void);
Block*	tolance(Block*, int);
void	touser(void*);
void	vecinit(void);
void	vector0(void);
void	vector80(void);
void	vmereset(void);
void	wbflush(void);
#define	waserror()	setlabel(&up->errlab[up->nerrlab++])
#define	kmapperm(x)	kmap(x)
#define KADDR(a)	((void*)((ulong)(a)|KZERO))
#define PADDR(a)	((ulong)(a)&~KZERO)
Page*	lkpage(Segment*, ulong);
void	lkpgfree(Page*);
#define vcacheinval(s)
