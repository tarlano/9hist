
#define	SET(x)	x = 0
#define	USED(x)	if(x)

Alarm*	alarm(int, void (*)(Alarm*), void*);
void	alarminit(void);
Block*	allocb(ulong);
int	anyready(void);
void	append(List**, List*);
Image*	attachimage(int, Chan*, ulong, ulong);
int	blen(Block *);
int	bround(Block *, int);
void	buzz(int, int);
void	cachepage(Page*, Image*);
void	cancel(Alarm*);
int	cangetc(void*);
int	canlock(Lock*);
int	canpage(Proc*);
int	canputc(void*);
int	canqlock(QLock*);
void	chandevinit(void);
void	chandevreset(void);
void	chaninit(void);
void	checkalarms(void);
void	clock(Ureg*);
Chan*	clone(Chan*, Chan*);
void	close(Chan*);
void	closeegrp(Egrp*);
void	closefgrp(Fgrp*);
void	closemount(Mount*);
void	closepgrp(Pgrp*);
long	clrfpintr(void);
void	confinit(void);
int	consactive(void);
Env*	copyenv(Env*, int);
void	copypage(Page*, Page*);
int	decref(Ref*);
void	delay(int);
void	delete(List**, List*, List*);
void	delete0(List**, List*);
Chan*	devattach(int, char*);
Chan*	devclone(Chan*, Chan*);
void	devdir(Chan*, Qid, char*, long, char*, long, Dir*);
long	devdirread(Chan*, char*, long, Dirtab*, int, Devgen*);
Devgen	devgen;
int	devno(int, int);
Chan*	devopen(Chan*, int, Dirtab*, int, Devgen*);
void	devstat(Chan*, char*, Dirtab*, int, Devgen*);
int	devwalk(Chan*, char*, Dirtab*, int, Devgen*);
void*	dmaalloc(ulong);
void	dumpqueues(void);
void	dumpregs(Ureg*);
void	dumpstack(void);
Fgrp*	dupfgrp(Fgrp*);
void	duppage(Page*);
Segment*	dupseg(Segment*);
void	dupswap(Page*);
void	envcpy(Egrp*, Egrp*);
void	envpgclose(Env*);
void	envpgcopy(Env*, Env*);
int	eqchan(Chan*, Chan*, long);
int	eqqid(Qid, Qid);
void	error(int);
void	errors(char*);
void	execpc(ulong);
void	exit(void);
Block*	expandb(Block *, int);
int	fault(ulong, int);
void	fdclose(int, int);
Chan*	fdtochan(int, int, int);
void	firmware(void);
int	fixfault(Segment*, ulong, int, int);
void	flowctl(Queue*);
void	flushmmu(void);
void	freealarm(Alarm*);
void	freeb(Block*);
int	freebroken(void);
void	freechan(Chan*);
void	freepte(Segment*, Pte*);
void	freesegs(int);
void	freewaitq(Waitq*);
Block*	getb(Blist*);
int	getc(IOQ*);
void	getcolor(ulong, ulong*, ulong*, ulong*);
int	getfields(char*, char**, int, char);
Block*	getq(Queue*);
int	gets(IOQ*, void*, int);
void	gotolabel(Label*);
void	grpinit(void);
int	hwcursmove(int, int);
int	hwcursset(uchar*, uchar*, int, int);
void*	ialloc(ulong, int);
long	ibrk(ulong, int);
int	incref(Ref*);
void	initq(IOQ*);
void	initseg(void);
void	insert(List**, List*, List*);
void	invalidateu(void);
void	isdir(Chan*);
int	ispages(void*);
void	kbdclock(void);
int	kbdcr2nl(IOQ*, int);
int	kbdputc(IOQ*, int);
void	kbdrepeat(int);
void	kickpager(void);
int	kprint(char*, ...);
void	kproc(char*, void(*)(void*), void*);
void	ksetenv(char*, char*);
void	ksetterm(char*);
int	latin1(int, int);
void	lights(int);
void	lock(Lock*);
void	lockinit(void);
void	lockpage(Page*);
Page*	lookpage(Image*, ulong);
void	machinit(void);
void	mapstack(Proc*);
void	mfreeseg(Segment*, ulong, int);
void	mmurelease(Proc*);
void	mntdump(void);
int	mount(Chan*, Chan*, int);
void	mouseclock(void);
int	mouseputc(IOQ*, int);
Chan*	namec(char*, int, int, ulong);
void	nameok(char*);
int	netgen(Chan*, void*, int, int, Dir*);
Chan*	netopen(Chan*, int, Network*);
long	netread(Chan*, void*, long, ulong, Network*);
void	netstat(Chan*, char*, Network*);
int	netwalk(Chan*, char*, Network*);
Alarm*	newalarm(void);
Chan*	newchan(void);
Egrp*	newegrp(void);
Fgrp*	newfgrp(void);
Mount*	newmount(Mhead*, Chan*);
Page*	newpage(int, Segment **, ulong);
Pgrp*	newpgrp(void);
Proc*	newproc(void);
void	newqinfo(Qinfo*);
Segment*	newseg(int, ulong, ulong);
Waitq*	newwaitq(void);
char*	nextelem(char*, char*);
void	nexterror(void);
void	notify(Ureg*);
void	nullput(Queue*, Block*);
int	openmode(ulong);
Block*	padb(Block*, int);
void	pageinit(void);
int	pagemeis0(void*);
void	panic(char*, ...);
void	pexit(char*, int);
void	pgrpcpy(Pgrp*, Pgrp*);
void	pgrpnote(Pgrp*, char*, long, int);
Pgrp*	pgrptab(int);
void	pio(Segment *, ulong, ulong, Page **);
#define	poperror()	u->nerrlab--
int	postnote(Proc*, int, char*, int);
int	pprint(char*, ...);
void	printinit(void);
ulong	procalarm(ulong);
void	procctl(Proc*);
void	procdump(void);
void	procinit0(void);
Proc*	proctab(int);
Pte*	ptealloc(void);
Pte*	ptecpy(Pte*);
Block*	pullup(Block *, int);
Queue*	pushq(Stream*, Qinfo*);
int	putb(Blist*, Block*);
void	putbq(Blist*, Block*);
int	putc(IOQ*, int);
void	putimage(Image*);
void	putmmu(ulong, ulong, Page*);
void	putpage(Page*);
int	putq(Queue*, Block*);
void	puts(IOQ*, void*, int);
void	putseg(Segment*);
void	putstr(char*);
void	putstr(char*);
void	putstrn(char*, long);
void	putswap(Page*);
ulong	pwait(Waitmsg*);
void	qlock(QLock*);
void	qunlock(QLock*);
int	readnum(ulong, char*, ulong, ulong, int);
void	ready(Proc*);
void	relocateseg(Segment*, ulong);
void	resched(char*);
void	resrcwait(char*);
int	return0(void*);
Proc*	runproc(void);
void	savefpregs(FPsave*);
void	sccclock(void);
void	sccintr(void);
void	sccsetup(void*);
void	sccspecial(int, IOQ*, IOQ*, int);
void	sched(void);
void	schedinit(void);
int	screenbits(void);
long	seconds(void);
Segment*	seg(Proc*, ulong, int);
ulong	segattach(Proc*, ulong, char *, ulong, ulong);
void	segpage(Segment*, Page*);
int	setcolor(ulong, ulong, ulong, ulong);
int	setlabel(Label*);
void	setswapchan(Chan*);
void	simpleputpage(Page*);
char*	skipslash(char*);
void	sleep(Rendez*, int(*)(void*), void*);
int	splhi(void);
int	spllo(void);
void	splx(int);
int	streamclose(Chan*);
int	streamclose1(Stream*);
int	streamenter(Stream*);
int	streamexit(Stream*, int);
Devgen	streamgen;
void	streaminit(void);
void	streaminit0(void);
Stream*	streamnew(ushort, ushort, ushort, Qinfo*, int);
void	streamopen(Chan*, Qinfo*);
int	streamparse(char*, Block*);
long	streamread(Chan*, void*, long);
void	streamstat(Chan*, char*, char*);
long	streamwrite(Chan*, void*, long, int);
long	stringread(Chan*, void*, long, char*, ulong);
void	swapinit(void);
long	syscall(Ureg*);
void	tcpinit(void);
void	tsleep(Rendez*, int (*)(void*), void*, int);
void	twakeme(Alarm*);
void	uncachepage(Page*);
long	unionread(Chan*, void*, long);
void	unlock(Lock*);
void	unlockpage(Page*);
void	userinit(void);
void	validaddr(ulong, ulong, int);
void*	vmemchr(void*, int, int);
void	wakeme(Alarm*);
void	wakeup(Rendez*);
void	rlock(RWlock*);
void	runlock(RWlock*);
void	wlock(RWlock*);
void	wunlock(RWlock*);
Mhead*	newmnthead(void);
void	mountfree(Mount*);
void	mntheadfree(Mhead*);
void	unmount(Chan*, Chan*);
