#undef	CHDIR	/* BUG */
#include "/sys/src/libc/9syscall/sys.h"

typedef long Syscall(ulong*);

Syscall sysr1;
Syscall syserrstr;
Syscall sysbind;
Syscall syschdir;
Syscall sysclose;
Syscall sysdup;
Syscall sysalarm;
Syscall sysexec;
Syscall sysexits;
Syscall sysfsession;
Syscall sysfauth;
Syscall sysfstat;
Syscall syssegbrk;
Syscall sysmount;
Syscall sysopen;
Syscall sysread;
Syscall sysseek;
Syscall syssleep;
Syscall sysstat;
Syscall sysrfork;
Syscall syswrite;
Syscall syspipe;
Syscall syscreate;
Syscall sysfd2path;
Syscall sysbrk_;
Syscall sysremove;
Syscall syswstat;
Syscall sysfwstat;
Syscall sysnotify;
Syscall sysnoted;
Syscall syssegattach;
Syscall syssegdetach;
Syscall syssegfree;
Syscall syssegflush;
Syscall sysrendezvous;
Syscall sysunmount;
Syscall syswait;
Syscall syswrite9p;
Syscall sysread9p;
Syscall	sysdeath;

Syscall *systab[]={
	[SYSR1]		sysr1,
	[ERRSTR]	syserrstr,
	[BIND]		sysbind,
	[CHDIR]		syschdir,
	[CLOSE]		sysclose,
	[DUP]		sysdup,
	[ALARM]		sysalarm,
	[EXEC]		sysexec,
	[EXITS]		sysexits,
	[FSESSION]	sysfsession,
	[FAUTH]		sysfauth,
	[FSTAT]		sysfstat,
	[SEGBRK]	syssegbrk,
	[MOUNT]		sysmount,
	[OPEN]		sysopen,
	[READ]		sysread,
	[SEEK]		sysseek,
	[SLEEP]		syssleep,
	[STAT]		sysstat,
	[RFORK]		sysrfork,
	[WRITE]		syswrite,
	[PIPE]		syspipe,
	[CREATE]	syscreate,
	[FD2PATH]	sysfd2path,
	[BRK_]		sysbrk_,
	[REMOVE]	sysremove,
	[WSTAT]		syswstat,
	[FWSTAT]	sysfwstat,
	[NOTIFY]	sysnotify,
	[NOTED]		sysnoted,
	[SEGATTACH]	syssegattach,
	[SEGDETACH]	syssegdetach,
	[SEGFREE]	syssegfree,
	[SEGFLUSH]	syssegflush,
	[RENDEZVOUS]	sysrendezvous,
	[UNMOUNT]	sysunmount,
	[WAIT]		syswait,
	[WRITE9P]	syswrite9p,
	[READ9P]	sysread9p,
};

char *sysctab[]={
	[SYSR1]		"Running",
	[ERRSTR]	"Errstr",
	[BIND]		"Bind",
	[CHDIR]		"Chdir",
	[CLOSE]		"Close",
	[DUP]		"Dup",
	[ALARM]		"Alarm",
	[EXEC]		"Exec",
	[EXITS]		"Exits",
	[FSESSION]	"Fsession",
	[FAUTH]		"Fauth",
	[FSTAT]		"Fstat",
	[SEGBRK]	"Segbrk",
	[MOUNT]		"Mount",
	[OPEN]		"Open",
	[READ]		"Read",
	[SEEK]		"Seek",
	[SLEEP]		"Sleep",
	[STAT]		"Stat",
	[RFORK]		"Rfork",
	[WRITE]		"Write",
	[PIPE]		"Pipe",
	[CREATE]	"Create",
	[FD2PATH]	"Fd2path",
	[BRK_]		"Brk",
	[REMOVE]	"Remove",
	[WSTAT]		"Wstat",
	[FWSTAT]	"Fwstat",
	[NOTIFY]	"Notify",
	[NOTED]		"Noted",
	[SEGATTACH]	"Segattach",
	[SEGDETACH]	"Segdetach",
	[SEGFREE]	"Segfree",
	[SEGFLUSH]	"Segflush",
	[RENDEZVOUS]	"Rendez",
	[UNMOUNT]	"Unmount",
	[WAIT]		"Wait",
	[WRITE9P]	"Write9p",
	[READ9P]	"Read9p",
};
