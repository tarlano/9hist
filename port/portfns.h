#define	SET(x)	x = 0
#define	USED(x)	if(x)

void	alarminit(void);
Alarm*	alarm(int, void (*)(Alarm*), void*);
Block*	allocb(ulong);
int	anyready(void);
void	append(List**, List*);
int	blen(Block *);
int	bround(Block *, int);
void	cancel(Alarm*);
int	canlock(Lock*);
int	canqlock(QLock*);
void	chandevinit(void);
void	chandevreset(void);
void	chaninit(void);
void	checkalarms(void);
void	clock(Ureg*);
Chan*	clone(Chan*, Chan*);
void	close(Chan*);
void	closemount(Mount*);
void	closepgrp(Pgrp*);
long	clrfpintr(void);
int	compactpte(Orig*, ulong);
void	confinit(void);
Env*	copyenv(Env*, int);
int	decref(Ref*);
void	delay(int);
void	delete0(List**, List*);
void	delete(List**, List*, List*);
Chan*	devattach(int, char*);
Chan*	devclone(Chan*, Chan*);
void	devdir(Chan*, Qid, char*, long, long, Dir*);
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
void	envpgclose(Env *);
int	eqchan(Chan*, Chan*, long);
int	eqqid(Qid, Qid);
void	error(int);
void	errors(char*);
void	execpc(ulong);
void	exit(void);
int	fault(ulong, int);
void	fdclose(int);
Chan*	fdtochan(int, int);
void	firmware(void);
void	flowctl(Queue*);
void	flushmmu(void);
void	forkmod(Seg*, Seg*, Proc*);
void	freealarm(Alarm*);
void	freeb(Block*);
int	freebroken(void);
void	freechan(Chan*);
void	freepage(Orig*, int);
void	freepte(Orig*);
void	freesegs(int);
Block*	getb(Blist*);
int	getfields(char*, char**, int, char);
Block*	getq(Queue*);
void	gotolabel(Label*);
void	growpte(Orig*, ulong);
void*	ialloc(ulong, int);
int	incref(Ref*);
void	insert(List**, List*, List*);
void	invalidateu(void);
void	isdir(Chan*);
void	kproc(char*, void(*)(void*), void*);
void	lock(Lock*);
void	lockinit(void);
Orig*	lookorig(ulong, ulong, int, Chan*);
void	machinit(void);
void	mapstack(Proc*);
void	mmurelease(Proc*);
int	mount(Chan*, Chan*, int);
Chan*	namec(char*, int, int, ulong);
Alarm*	newalarm(void);
Chan*	newchan(void);
PTE*	newmod(Orig*);
Mount*	newmount(void);
Orig*	neworig(ulong, ulong, int, Chan*);
Page*	newpage(int, Orig*, ulong);
Pgrp*	newpgrp(void);
Proc*	newproc(void);
void	newqinfo(Qinfo*);
char*	nextelem(char*, char*);
void	nexterror(void);
void	notify(Ureg*);
void	nullput(Queue*, Block*);
int	openmode(ulong);
Block*	padb(Block*, int);
void	pageinit(void);
void	panic(char*, ...);
void	pexit(char*, int);
void	pgrpcpy(Pgrp*, Pgrp*);
void	pgrpinit(void);
void	pgrpnote(Pgrp*, char*, long, int);
Pgrp*	pgrptab(int);
#define	poperror()	u->nerrlab--
int	postnote(Proc*, int, char*, int);
int	pprint(char*, ...);
void	printslave(void);
ulong 	procalarm(ulong);
void	procinit0(void);
Proc*	proctab(int);
Block*	pullup(Block *, int);
Queue*	pushq(Stream*, Qinfo*);
int	putb(Blist*, Block*);
void	putbq(Blist*, Block*);
void	putmmu(ulong, ulong);
int	putq(Queue*, Block*);
ulong	pwait(Waitmsg*);
void	qlock(QLock*);
void	qunlock(QLock*);
int	readnum(ulong, char*, ulong, ulong, int);
void	ready(Proc*);
int	return0(void*);
Proc*	runproc(void);
void	savefpregs(FPsave*);
void	schedinit(void);
void	sched(void);
long	seconds(void);
Seg*	seg(Proc*, ulong);
int	segaddr(Seg*, ulong, ulong);
int	setlabel(Label*);
char*	skipslash(char*);
void	sleep(Rendez*, int(*)(void*), void*);
int	splhi(void);
int	spllo(void);
void	splx(int);
int	streamclose1(Stream*);
int	streamclose(Chan*);
int	streamenter(Stream*);
int	streamexit(Stream*, int);
Devgen	streamgen;
void	streaminit0(void);
void	streaminit(void);
Stream*	streamnew(ushort, ushort, ushort, Qinfo*, int);
void	streamopen(Chan*, Qinfo*);
int	streamparse(char*, Block*);
long	streamread(Chan*, void*, long);
void	streamstat(Chan*, char*, char*);
long	streamwrite(Chan*, void*, long, int);
long	stringread(Chan*, void*, long, char*, ulong);
long	syscall(Ureg*);
void	tsleep(Rendez*, int (*)(void*), void*, int);
void	twakeme(Alarm*);
long	unionread(Chan*, void*, long);
void	unlock(Lock*);
void	unusepage(Page*, int);
void	usepage(Page*, int);
void	userinit(void);
void	validaddr(ulong, ulong, int);
void*	vmemchr(void*, int, int);
void	wakeme(Alarm*);
void	wakeup(Rendez*);
