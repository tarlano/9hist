Alarm	*alarm(int, void (*)(Alarm*), void*);
void	alarminit(void);
Block	*allocb(ulong);
int	anyready(void);
void	append(List**, List*);
void	arginit(void);
void	cancel(Alarm*);
int	canlock(Lock*);
int	canqlock(QLock*);
void	chaninit(void);
void	chandevreset(void);
void	chandevinit(void);
void	checkalarms(void);
void	clock(Ureg*);
void	clockinit(void);
Chan	*clone(Chan*, Chan*);
void	close(Chan*);
void	closemount(Mount*);
void	closepgrp(Pgrp*);
void	clearmmucache(void);
long	clrfpintr(void);
int	compactpte(Orig*, ulong);
ulong	confeval(char*);
void	confprint(void);
void	confinit(void);
void	confread(void);
void	confset(char*);
int	consactive(void);
int	conschar(void);
void	consoff(void);
int	consputc(int);
Env	*copyenv(Env*, int);
int	decref(Ref*);
void	delay(int);
void	delete(List**, List*, List*);
void	delete0(List**, List*);
Chan	*devattach(int, char*);
Chan	*devclone(Chan*, Chan*);
void	devdir(Chan*, Qid, char*, long, long, Dir*);
long	devdirread(Chan*, char*, long, Dirtab*, int, Devgen*);
Devgen	devgen;
int	devno(int, int);
Chan	*devopen(Chan*, int, Dirtab*, int, Devgen*);
void	devstat(Chan*, char*, Dirtab*, int, Devgen*);
int	devwalk(Chan*, char*, Dirtab*, int, Devgen*);
void	duartinit(void);
void	duartintr(void);
int	duartputc(int);
void	duartputs(char*);
void	duartreset(void);
void	duartxmit(int);
void	dumpregs(Ureg*);
void	dumpstack(void);
int	eqchan(Chan*, Chan*, long);
int	eqqid(Qid, Qid);
void	envpgclose(Env*);
void	error(int);
void	errors(char*);
void	evenaddr(ulong);
void	execpc(ulong);
void	exit(void);
int	fault(ulong, int);
void	faultmips(Ureg*, int, int);
ulong	fcr31(void);
void	fdclose(int);
Chan	*fdtochan(int, int);
void	firmware(void);
void	flowctl(Queue*);
void	flushmmu(void);
void	forkmod(Seg*, Seg*, Proc*);
void	freeb(Block*);
int	freebroken(void);
void	freepage(Orig*, int);
void	freepte(Orig*);
void	freesegs(int);
void	freealarm(Alarm*);
Block	*getb(Blist*);
int	getfields(char*, char**, int, char);
Block	*getq(Queue*);
void	gettlb(int, ulong*);
ulong	gettlbvirt(int);
void	gotolabel(Label*);
void	gotopc(ulong);
void	growpte(Orig*, ulong);
void	*ialloc(ulong, int);
void	icflush(void *, int);
int	incref(Ref*);
void	insert(List**, List*, List*);
void	intr(Ureg*);
void	invalidateu(void);
void	ioboardinit(void);
void	isdir(Chan*);
void	kbdchar(int);
void	kproc(char*, void(*)(void*), void*);
void	launchinit(void);
void	lanceintr(void);
void	lanceparity(void);
void	lancesetup(Lance*);
void	launch(int);
void	lights(int);
void	lock(Lock*);
void	lockinit(void);
Orig	*lookorig(ulong, ulong, int, Chan*);
void	machinit(void);
void	mapstack(Proc*);
int	mount(Chan*, Chan*, int);
Chan	*namec(char*, int, int, ulong);
void	nexterror(void);
Alarm	*newalarm(void);
Chan	*newchan(void);
PTE	*newmod(Orig*);
Mount	*newmount(void);
Orig	*neworig(ulong, ulong, int, Chan*);
Page	*newpage(int, Orig*, ulong);
Pgrp	*newpgrp(void);
Proc	*newproc(void);
void	newqinfo(Qinfo*);
char	*nextelem(char*, char*);
void	newstart(void);
int	newtlbpid(Proc*);
void	notify(Ureg*);
void	novme(int);
void	nullput(Queue*, Block*);
void	online(void);
int	openmode(ulong);
Block	*padb(Block*, int);
void	pageinit(void);
void	panic(char*, ...);
void	pexit(char*, int);
void	pgrpcpy(Pgrp*, Pgrp*);
void	pgrpinit(void);
void	pgrpnote(Pgrp*, char*, long, int);
Pgrp	*pgrptab(int);
int	postnote(Proc*, int, char*, int);
int	pprint(char*, ...);
Block	*prepend(Block*, int);
void	prflush(void);
void	printinit(void);
void	printslave(void);
void	procinit0(void);
Proc	*proctab(int);
void	purgetlb(int);
Queue	*pushq(Stream*, Qinfo*);
void	putmmu(ulong, ulong);
void	puttlb(ulong, ulong);
void	puttlbx(int, ulong, ulong);
int	putb(Blist*, Block*);
void	putbq(Blist*, Block*);
int	putq(Queue*, Block*);
void	putstrn(char*, long);
ulong	pwait(Waitmsg*);
int	readlog(ulong, char*, ulong);
int	readnum(ulong, char*, ulong, ulong, int);
void	ready(Proc*);
void	qlock(QLock*);
void	qunlock(QLock*);
void	restfpregs(FPsave*, ulong);
int	return0(void*);
Proc	*runproc(void);
void	savefpregs(FPsave*);
void	sched(void);
void	schedinit(void);
long	seconds(void);
Seg	*seg(Proc*, ulong);
int	segaddr(Seg*, ulong, ulong);
int	setlabel(Label*);
void	setvmevec(int, void (*)(int));
void	sinit(void);
char	*skipslash(char*);
void	sleep(Rendez*, int(*)(void*), void*);
uchar	*smap(int, uchar*);
int	splhi(void);
int	spllo(void);
void	splx(int);
void	sunmap(int, uchar*);
Devgen	streamgen;
int	streamclose(Chan*);
int	streamclose1(Stream*);
int	streamenter(Stream*);
int	streamexit(Stream*, int);
void	streaminit(void);
void	streaminit0(void);
long	streamread(Chan*, void*, long);
long	streamwrite(Chan*, void*, long, int);
Stream	*streamnew(ushort, ushort, ushort, Qinfo*, int);
void	streamopen(Chan*, Qinfo*);
int	streamparse(char*, Block*);
void	streamstat(Chan*, char*, char*);
long	stringread(Chan*, void*, long, char*, ulong);
long	syscall(Ureg*);
void	sysloginit(void);
void	syslog(char*, int);
void	tlbinit(void);
Block	*tolance(Block*, int);
void	touser(void *);
void	tsleep(Rendez*, int (*)(void*), void*, int);
void	twakeme(Alarm*);
void	unlock(Lock*);
void	unusepage(Page*, int);
void	usepage(Page*, int);
void	userinit(void);
void	validaddr(ulong, ulong, int);
void	vecinit(void);
void	vector80(void);
void	*vmemchr(void*, int, int);
void	vmereset(void);
void	wakeme(Alarm*);
void	wakeup(Rendez*);
void	wbflush(void);

#define	waserror()	setlabel(&u->errlab[u->nerrlab++])
#define	poperror()	u->nerrlab--

/*
 *  no external state to save on the SGI
 */
#define procsetup(p)	((p)->fpstate = FPinit)
#define procsave(x,y)
#define procrestore(x,y)

#define USED(x) if(x)
#define SET(x) x = 0
#define	flushvirt()
#define	flushpage(x)
