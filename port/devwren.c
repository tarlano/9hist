#include	"u.h"
#include	"lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"errno.h"
#include	"devtab.h"
#include	"io.h"

#define DATASIZE	(8*1024)

typedef struct Partition	Partition;
typedef struct Drive		Drive;

enum {
	Npart=		8+2,	/* 8 sub partitions, disk, and partition */
	Ndisk=		64,	/* maximum disks; if you change it, you must
				   map from dev to disk */

	/* file types */
	Qdir=		0,
};
#define PART(x)		((x)&0xF)
#define DRIVE(x)	(((x)>>4)&(Ndisk-1))
#define MKQID(d,p)	(((d)<<4) | (p))

struct Partition
{
	ulong	start;
	ulong	end;
	char	name[NAMELEN+1];
};

struct Drive
{
	ulong		bytes;			/* bytes per block */
	int		npart;			/* actual number of partitions */
	int		drive;
	Partition	p[Npart];
};

static Drive	wren[Ndisk];

static void	wrenpart(int);
static long	wrenio(Drive *, Partition *, int, char *, ulong, ulong);

/*
 *  accepts [0-7].[0-7], or abbreviation
 */
static int
wrendev(char *p)
{
	int drive, unit;

	if(p == 0 || p[0] == '\0')
		return 0;
	if(p[0] < '0' || p[0] > '7')
		errors("bad scsi drive specifier");
	drive = p[0] - '0';
	unit = 0;
	if(p[1]){
		if(p[1] != '.' || p[2] < '0' || p[2] > '7' || p[3] != '\0')
			errors("bad scsi unit specifier");
		unit = p[2] - '0';
	}
	return (drive << 3) | unit;
}

static int
wrengen(Chan *c, Dirtab *tab, long ntab, long s, Dir *dirp)
{
	Qid qid;
	int drive;
	char name[NAMELEN+4];
	Drive *dp;
	Partition *pp;
	ulong l;

	qid.vers = 0;
	drive = c->dev;
	if(drive >= Ndisk)
		return -1;
	dp = &wren[drive];

	if(s >= dp->npart)
		return -1;

	pp = &dp->p[s];
	if(drive & 7)
		sprint(name, "hd%d.%d%s", drive>>3, drive&7, pp->name);
	else
		sprint(name, "hd%d%s", drive>>3, pp->name);
	name[NAMELEN] = 0;
	qid.path = MKQID(drive, s);
	l = (pp->end - pp->start) * dp->bytes;
	devdir(c, qid, name, l, eve, 0600, dirp);
	return 1;
}

void
wrenreset(void)
{
}

void
wreninit(void)
{
}

/*
 *  param is #r<target>.<lun>
 */
Chan *
wrenattach(char *param)
{
	Chan *c;
	int drive;

	drive = wrendev(param);
	wrenpart(drive);
	c = devattach('w', param);
	c->dev = drive;
	return c;
}

Chan*
wrenclone(Chan *c, Chan *nc)
{
	return devclone(c, nc);
}

int
wrenwalk(Chan *c, char *name)
{
	return devwalk(c, name, 0, 0, wrengen);
}

void
wrenstat(Chan *c, char *dp)
{
	devstat(c, dp, 0, 0, wrengen);
}

Chan*
wrenopen(Chan *c, int omode)
{
	return devopen(c, omode, 0, 0, wrengen);
}

void
wrencreate(Chan *c, char *name, int omode, ulong perm)
{
	error(Eperm);
}

void
wrenclose(Chan *c)
{
}

void
wrenremove(Chan *c)
{
	error(Eperm);
}

void
wrenwstat(Chan *c, char *dp)
{
	error(Eperm);
}

long
wrenread(Chan *c, char *a, long n, ulong offset)
{
	Drive *d;
	Partition *p;


	if(c->qid.path == CHDIR)
		return devdirread(c, a, n, 0, 0, wrengen);

	d = &wren[DRIVE(c->qid.path)];
	if(d->npart == 0)
		errors("drive repartitioned");
	p = &d->p[PART(c->qid.path)];
	return wrenio(d, p, 0, a, n, offset);
}

long
wrenwrite(Chan *c, char *a, long n, ulong offset)
{
	Drive *d;
	Partition *p;

	d = &wren[DRIVE(c->qid.path)];
	if(d->npart == 0)
		errors("drive repartitioned");
	p = &d->p[PART(c->qid.path)];
	return wrenio(d, p, 1, a, n, offset);
}

static long
wrenio(Drive *d, Partition *p, int write, char *a, ulong n, ulong offset)
{
	Scsibuf *b;
	ulong block;

	if(n % d->bytes || offset % d->bytes)
		errors("io not block aligned");
	block = offset / d->bytes + p->start;
	if(block >= p->end)
		return 0;
	if(n > DATASIZE)
		n = DATASIZE;
	n /= d->bytes;
	if(block + n > p->end)
		n = p->end - block;
	if(n == 0)
		return 0;
	b = scsibuf();
	if(waserror()){
		scsifree(b);
		nexterror();
	}
	if(write){
		memmove(b->virt, a, n*d->bytes);
		n = scsibwrite(d->drive, b, n, d->bytes, block);
	}else{
		n = scsibread(d->drive, b, n, d->bytes, block);
		memmove(a, b->virt, n);
	}
	poperror();
	scsifree(b);
	return n;
}

/*
 *  read partition table.  The partition table is just ascii strings.
 */
#define MAGIC "plan9 partitions"
static void
wrenpart(int dev)
{
	Drive *dp;
	Partition *pp;
	uchar buf[32];
	Scsibuf *b;
	char *rawpart, *line[Npart+1], *field[3];
	ulong n;
	int i;

	scsiready(dev);
	scsisense(dev, buf);
	scsicap(dev, buf);
	dp = &wren[dev];
	dp->drive = dev;

	/*
	 *  we always have a partition for the whole disk
	 *  and one for the partition table
	 */
	dp->bytes = (buf[4]<<24)+(buf[5]<<16)+(buf[6]<<8)+(buf[7]);
	pp = &dp->p[0];
	strcpy(pp->name, "disk");
	pp->start = 0;
	pp->end = (buf[0]<<24)+(buf[1]<<16)+(buf[2]<<8)+(buf[3]) + 1;
	pp++;
	strcpy(pp->name, "partition");
	pp->start = dp->p[0].end - 1;
	pp->end = dp->p[0].end;
	dp->npart = 2;

	/*
	 *  read partition table from disk, null terminate
	 */
	b = scsibuf();
	if(waserror()){
		scsifree(b);
		nexterror();
	}
	scsibread(dev, b, 1, dp->bytes, dp->p[0].end-1);
	rawpart = b->virt;
	rawpart[dp->bytes-1] = 0;

	/*
	 *  parse partition table.
	 */
	n = getfields(rawpart, line, Npart+1, '\n');
	if(strncmp(line[0], MAGIC, sizeof(MAGIC)-1) == 0){
		for(i = 1; i < n; i++){
			pp++;
			if(getfields(line[i], field, 3, ' ') != 3){
				break;
			}
			strncpy(pp->name, field[0], NAMELEN);
			pp->start = strtoul(field[1], 0, 0);
			pp->end = strtoul(field[2], 0, 0);
			if(pp->start > pp->end || pp->start >= dp->p[0].end){
				break;
			}
			dp->npart++;
		}
	}
	poperror();
	scsifree(b);
}
