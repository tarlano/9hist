#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<memlayer.h>

enum
{
	Qtopdir		= 1,
	Q2nd,
	Qnew,
	Q3rd,
	Qcolormap,
	Qctl,
	Qdata,
	Qrefresh,
};

/*
 * Qid path is:
 *	 4 bits of file type (qids above)
 *	24 bits of mux slot number +1; 0 means not attached to client
 */
#define	QSHIFT	4	/* location in qid of client # */

#define	QID(q)		(((q).path&0x0000000F)>>0)
#define	CLIENTPATH(q)	((q&0x07FFFFFF0)>>QSHIFT)
#define	CLIENT(q)	CLIENTPATH((q).path)

#define	NHASH		(1<<5)
#define	HASHMASK	(NHASH-1)

typedef struct Client Client;
typedef struct Draw Draw;
typedef struct DImage DImage;
typedef struct DScreen DScreen;
typedef struct CScreen CScreen;
typedef struct FChar FChar;
typedef struct Refresh Refresh;
typedef struct Refx Refx;
typedef struct DName DName;

#define	BLANKTIME	30*60*1000	/* half hour */

struct Draw
{
	QLock;
	int		clientid;
	int		nclient;
	Client**	client;
	int		nname;
	DName*	name;
	int		vers;
	int		blanked;	/* screen turned off */
	ulong		blanktime;	/* time of last operation */
	ulong		savemap[3*256];
};

struct Client
{
	Ref		r;
	DImage*		dimage[NHASH];
	CScreen*	cscreen;
	Refresh*	refresh;
	Rendez		refrend;
	uchar*		readdata;
	int		nreaddata;
	int		busy;
	int		clientid;
	int		slot;
	int		refreshme;
	int		infoid;
};

struct Refresh
{
	DImage*		dimage;
	Rectangle	r;
	Refresh*	next;
};

struct Refx
{
	Client*		client;
	DImage*		dimage;
};

struct DName
{
	char			*name;
	Client	*client;
	DImage*		dimage;
	int			vers;
};

struct FChar
{
	int		minx;	/* left edge of bits */
	int		maxx;	/* right edge of bits */
	uchar		miny;	/* first non-zero scan-line */
	uchar		maxy;	/* last non-zero scan-line + 1 */
	schar		left;	/* offset of baseline */
	uchar		width;	/* width of baseline */
};

/*
 * Reference counts in DImages:
 *	one per open by original client
 *	one per screen image or fill
 * Images published by name are not ref'ed, but protected by
 * comparing version number with the DName entry.
 */
struct DImage
{
	int		id;
	int		ref;
	char		*name;
	int		vers;
	Memimage*	image;
	int		ascent;
	int		nfchar;
	FChar*		fchar;
	DScreen*	dscreen;	/* 0 if not a window */
	DImage*		next;
};

struct CScreen
{
	DScreen*	dscreen;
	CScreen*	next;
};

struct DScreen
{
	int		id;
	int		public;
	int		ref;
	DImage	*dimage;
	DImage	*dfill;
	Memscreen*	screen;
	Client*		owner;
	DScreen*	next;
};

static	Draw		sdraw;
static	Memimage	screenimage;
static	Memdata	screendata;
static	Rectangle	flushrect;
static	DScreen*	dscreen;
extern	void		flushmemscreen(Rectangle);
	void		drawmesg(Client*, void*, int);
	void		drawuninstall(Client*, int);
	void		drawfreedimage(DImage*);

static	char Enodrawimage[] =	"unknown id for draw image";
static	char Enodrawscreen[] =	"unknown id for draw screen";
static	char Eshortdraw[] =	"short draw message";
static	char Eshortread[] =	"draw read too short";
static	char Eimageexists[] =	"image id in use";
static	char Escreenexists[] =	"screen id in use";
static	char Edrawmem[] =	"image memory allocation failed";
static	char Ereadoutside[] =	"readimage outside image";
static	char Ewriteoutside[] =	"writeimage outside image";
static	char Enotfont[] =	"image not a font";
static	char Eindex[] =		"character index out of range";
static	char Enoclient[] =	"no such draw client";
static	char Eldepth[] =	"image has bad ldepth";
static	char Enameused[] =	"image name in use";
static	char Enoname[] =	"no image with that name";
static	char Eoldname[] =	"named image no longer valid";
static	char Enamed[] = 	"image already has name";
static	char Ewrongname[] = 	"wrong name for image";

static int
drawgen(Chan *c, Dirtab *tab, int x, int s, Dir *dp)
{
	int t;
	Qid q;
	ulong path;
	Client *cl;
	char buf[NAMELEN];

	USED(tab, x);
	q.vers = 0;

	/*
	 * Top level directory contains the name of the device.
	 */
	if(c->qid.path == CHDIR){
		switch(s){
		case 0:
			q = (Qid){CHDIR|Q2nd, 0};
			devdir(c, q, "draw", 0, eve, 0555, dp);
			break;
		default:
			return -1;
		}
		return 1;
	}

	/*
	 * Second level contains "new" plus all the clients.
	 */
	t = QID(c->qid);
	if(t == Q2nd || t == Qnew){
		if(s == 0){
			q = (Qid){Qnew, 0};
			devdir(c, q, "new", 0, eve, 0666, dp);
		}
		else if(s <= sdraw.nclient){
			cl = sdraw.client[s-1];
			if(cl == 0)
				return 0;
			sprint(buf, "%d", cl->clientid);
			q = (Qid){CHDIR|(s<<QSHIFT)|Q3rd, 0};
			devdir(c, q, buf, 0, eve, 0555, dp);
			return 1;
		}
		else
			return -1;
		return 1;
	}

	/*
	 * Third level.
	 */
	path = c->qid.path&~(CHDIR|((1<<QSHIFT)-1));	/* slot component */
	q.vers = c->qid.vers;
	switch(s){
	case 0:
		q = (Qid){path|Qcolormap, c->qid.vers};
		devdir(c, q, "colormap", 0, eve, 0600, dp);
		break;
	case 1:
		q = (Qid){path|Qctl, c->qid.vers};
		devdir(c, q, "ctl", 0, eve, 0600, dp);
		break;
	case 2:
		q = (Qid){path|Qdata, c->qid.vers};
		devdir(c, q, "data", 0, eve, 0600, dp);
		break;
	case 3:
		q = (Qid){path|Qrefresh, c->qid.vers};
		devdir(c, q, "refresh", 0, eve, 0400, dp);
		break;
	default:
		return -1;
	}
	return 1;
}

static
int
drawrefactive(void *a)
{
	Client *c;

	c = a;
	return c->refreshme || c->refresh!=0;
}

static
void
drawrefresh(Memimage *l, Rectangle r, void *v)
{
	Refx *x;
	DImage *d;
	Client *c;
	Refresh *ref;

	USED(l);
	if(v == 0)
		return;
	x = v;
	c = x->client;
	d = x->dimage;
	for(ref=c->refresh; ref; ref=ref->next)
		if(ref->dimage == d){
			bbox(&ref->r, r);
			return;
		}
	ref = malloc(sizeof(Refresh));
	if(ref){
		ref->dimage = d;
		ref->r = r;
		ref->next = c->refresh;
		c->refresh = ref;
	}
}

static
void
addflush(Rectangle r)
{
	bbox(&flushrect, r);
}

static
void
dstflush(int dstid, Memimage *dst, Rectangle r)
{
	Memlayer *l;

	if(dstid == 0){
		bbox(&flushrect, r);
		return;
	}
	l = dst->layer;
	if(l == nil)
		return;
	do{
		if(l->screen->image->data != screenimage.data)
			return;
		r = rectaddpt(r, l->delta);
		l = l->screen->image->layer;
	}while(l);
	bbox(&flushrect, r);
}

static
void
drawflush(void)
{
	flushmemscreen(flushrect);
	flushrect = Rect(10000, 10000, -10000, -10000);
}

static
int
drawcmp(char *a, char *b, int n)
{
	if(strlen(a) != n)
		return 1;
	return memcmp(a, b, n);
}

DName*
drawlookupname(int n, char *str)
{
	DName *name, *ename;

	name = sdraw.name;
	ename = &name[sdraw.nname];
	for(; name<ename; name++)
		if(drawcmp(name->name, str, n) == 0)
			return name;
	return 0;
}

int
drawgoodname(DImage *d)
{
	DName *n;

	/* if window, validate the screen's own images */
	if(d->dscreen)
		if(drawgoodname(d->dscreen->dimage) == 0
		|| drawgoodname(d->dscreen->dfill) == 0)
			return 0;
	if(d->name == nil)
		return 1;
	n = drawlookupname(strlen(d->name), d->name);
	if(n==nil || n->vers!=d->vers)
		return 0;
	return 1;
}

DImage*
drawlookup(Client *client, int id, int checkname)
{
	DImage *d;

	d = client->dimage[id&HASHMASK];
	while(d){
		if(d->id == id){
			if(checkname && !drawgoodname(d))
				error(Eoldname);
			return d;
		}
		d = d->next;
	}
	return 0;
}

DScreen*
drawlookupdscreen(int id)
{
	DScreen *s;

	s = dscreen;
	while(s){
		if(s->id == id)
			return s;
		s = s->next;
	}
	return 0;
}

DScreen*
drawlookupscreen(Client *client, int id, CScreen **cs)
{
	CScreen *s;

	s = client->cscreen;
	while(s){
		if(s->dscreen->id == id){
			*cs = s;
			return s->dscreen;
		}
		s = s->next;
	}
	error(Enodrawscreen);
	return 0;
}

Memimage*
drawinstall(Client *client, int id, Memimage *i, DScreen *dscreen)
{
	DImage *d;

	d = malloc(sizeof(DImage));
	if(d == 0)
		return 0;
	d->id = id;
	d->ref = 1;
	d->name = 0;
	d->vers = 0;
	d->image = i;
	d->nfchar = 0;
	d->fchar = 0;
	d->dscreen = dscreen;
	d->next = client->dimage[id&HASHMASK];
	client->dimage[id&HASHMASK] = d;
	return i;
}

Memscreen*
drawinstallscreen(Client *client, DScreen *d, int id, DImage *dimage, DImage *dfill, int public)
{
	Memscreen *s;
	CScreen *c;

	c = malloc(sizeof(CScreen));
	if(c == 0)
		return 0;
	if(d == 0){
		d = malloc(sizeof(DScreen));
		if(d == 0){
			free(c);
			return 0;
		}
		s = malloc(sizeof(Memscreen));
		if(s == 0){
			free(c);
			free(d);
			return 0;
		}
		s->frontmost = 0;
		s->rearmost = 0;
		d->dimage = dimage;
		if(dimage){
			s->image = dimage->image;
			dimage->ref++;
		}
		d->dfill = dfill;
		if(dfill){
			s->fill = dfill->image;
			dfill->ref++;
		}
		d->ref = 0;
		d->id = id;
		d->screen = s;
		d->public = public;
		d->next = dscreen;
		d->owner = client;
		dscreen = d;
	}
	c->dscreen = d;
	d->ref++;
	c->next = client->cscreen;
	client->cscreen = c;
	return d->screen;
}

void
drawdelname(DName *name)
{
	int i;

	i = name-sdraw.name;
	memmove(name, name+1, (sdraw.nname-(i+1))*sizeof(DName));
	sdraw.nname--;
}

void
drawfreedscreen(DScreen *this)
{
	DScreen *ds, *next;

	this->ref--;
	if(this->ref < 0)
		print("negative ref in drawfreedscreen\n");
	if(this->ref > 0)
		return;
	ds = dscreen;
	if(ds == this){
		dscreen = this->next;
		goto Found;
	}
	while(next = ds->next){	/* assign = */
		if(next == this){
			ds->next = this->next;
			goto Found;
		}
		ds = next;
	}
	error(Enodrawimage);

    Found:
	if(this->dimage)
		drawfreedimage(this->dimage);
	if(this->dfill)
		drawfreedimage(this->dfill);
	free(this->screen);
	free(this);
}

void
drawfreedimage(DImage *dimage)
{
	int i;
	Memimage *l;
	DScreen *ds;

	dimage->ref--;
	if(dimage->ref < 0)
		print("negative ref in drawfreedimage\n");
	if(dimage->ref > 0)
		return;

	/* any names? */
	for(i=0; i<sdraw.nname; )
		if(sdraw.name[i].dimage == dimage)
			drawdelname(sdraw.name+i);
		else
			i++;
	if(dimage->vers != 0)	/* acquired by name; owned by someone else*/
		goto Return;
	if(dimage->image == &screenimage)	/* don't free the display */
		goto Return;
	ds = dimage->dscreen;
	if(ds){
		l = dimage->image;
		if(l->data == screenimage.data)
			addflush(l->layer->screenr);
		if(l->layer->refreshfn == drawrefresh)	/* else true owner will clean up */
			free(l->layer->refreshptr);
		l->layer->refreshptr = nil;
		if(drawgoodname(dimage))
			memldelete(l);
		else
			memlfree(l);
		drawfreedscreen(ds);
	}else
		freememimage(dimage->image);
    Return:
	free(dimage->fchar);
	free(dimage);
}

void
drawuninstallscreen(Client *client, CScreen *this)
{
	CScreen *cs, *next;

	cs = client->cscreen;
	if(cs == this){
		client->cscreen = this->next;
		drawfreedscreen(this->dscreen);
		free(this);
		return;
	}
	while(next = cs->next){	/* assign = */
		if(next == this){
			cs->next = this->next;
			drawfreedscreen(this->dscreen);
			free(this);
			return;
		}
		cs = next;
	}
}

void
drawuninstall(Client *client, int id)
{
	DImage *d, *next;

	d = client->dimage[id&HASHMASK];
	if(d == 0)
		error(Enodrawimage);
	if(d->id == id){
		client->dimage[id&HASHMASK] = d->next;
		drawfreedimage(d);
		return;
	}
	while(next = d->next){	/* assign = */
		if(next->id == id){
			d->next = next->next;
			drawfreedimage(next);
			return;
		}
		d = next;
	}
	error(Enodrawimage);
}

void
drawaddname(Client *client, DImage *di, int n, char *str)
{
	DName *name, *ename, *new, *t;

	name = sdraw.name;
	ename = &name[sdraw.nname];
	for(; name<ename; name++)
		if(drawcmp(name->name, str, n) == 0)
			error(Enameused);
	t = smalloc((sdraw.nname+1)*sizeof(DName));
	memmove(t, sdraw.name, sdraw.nname*sizeof(DName));
	free(sdraw.name);
	sdraw.name = t;
	new = &sdraw.name[sdraw.nname++];
	new->name = smalloc(n+1);
	memmove(new->name, str, n);
	new->name[n] = 0;
	new->dimage = di;
	new->client = client;
	new->vers = ++sdraw.vers;
}

Client*
drawnewclient(void)
{
	Client *cl, **cp;
	int i;

	for(i=0; i<sdraw.nclient; i++){
		cl = sdraw.client[i];
		if(cl == 0)
			break;
	}
	if(i == sdraw.nclient){
		cp = malloc((sdraw.nclient+1)*sizeof(Client*));
		if(cp == 0)
			return 0;
		memmove(cp, sdraw.client, sdraw.nclient*sizeof(Client*));
		free(sdraw.client);
		sdraw.client = cp;
		sdraw.nclient++;
		cp[i] = 0;
	}
	cl = malloc(sizeof(Client));
	if(cl == 0)
		return 0;
	memset(cl, 0, sizeof(Client));
	cl->slot = i;
	cl->clientid = ++sdraw.clientid;
	sdraw.client[i] = cl;
	return cl;
}

Client*
drawclientofpath(ulong path)
{
	Client *cl;
	int slot;

	slot = CLIENTPATH(path);
	if(slot == 0)
		return nil;
	cl = sdraw.client[slot-1];
	if(cl==0 || cl->clientid==0)
		return nil;
	return cl;
}


Client*
drawclient(Chan *c)
{
	Client *client;

	client = drawclientofpath(c->qid.path);
	if(client == nil)
		error(Enoclient);
	return client;
}

Memimage*
drawimage(Client *client, uchar *a)
{
	DImage *d;

	d = drawlookup(client, BGLONG(a), 1);
	if(d == nil)
		error(Enodrawimage);
	return d->image;
}

void
drawrectangle(Rectangle *r, uchar *a)
{
	r->min.x = BGLONG(a+0*4);
	r->min.y = BGLONG(a+1*4);
	r->max.x = BGLONG(a+2*4);
	r->max.y = BGLONG(a+3*4);
}

void
drawpoint(Point *p, uchar *a)
{
	p->x = BGLONG(a+0*4);
	p->y = BGLONG(a+1*4);
}

Point
drawchar(Memimage *dst, Point p, Memimage *src, Point *sp, DImage *font, int index)
{
	FChar *fc;
	Rectangle r;
	Point sp1;

	fc = &font->fchar[index];
	r.min.x = p.x+fc->left;
	r.min.y = p.y-(font->ascent-fc->miny);
	r.max.x = r.min.x+(fc->maxx-fc->minx);
	r.max.y = r.min.y+(fc->maxy-fc->miny);
	sp1.x = sp->x+fc->left;
	sp1.y = sp->y+fc->miny;
	memdraw(dst, r, src, sp1, font->image, Pt(fc->minx, fc->miny));
	p.x += fc->width;
	sp->x += fc->width;
	return p;
}

Chan*
drawattach(char *spec)
{
	int width;

	if(screendata.data == nil){
		screendata.base = nil;
		screendata.data = attachscreen(&screenimage.r, &screenimage.ldepth, &width);
		if(screendata.data != nil){
			screendata.ref = 1;
			screenimage.data = &screendata;
			screenimage.width = width;
			screenimage.clipr  = screenimage.r;
		}
	}
	return devattach('i', spec);
}

int
drawwalk(Chan *c, char *name)
{
	if(screendata.data == nil)
		error("no frame buffer");
	if(strcmp(name, "..") == 0){
		switch(QID(c->qid)){
		case Qtopdir:
		case Q2nd:
			c->qid = (Qid){CHDIR|Qtopdir, 0};
			break;
		case Q3rd:
			c->qid = (Qid){CHDIR|Q2nd, 0};
			break;
		default:
			panic("drawwalk %lux", c->qid.path);
		}
		return 1;
	}
	return devwalk(c, name, 0, 0, drawgen);
}

static void
drawstat(Chan *c, char *db)
{
	devstat(c, db, 0, 0, drawgen);
}

static Chan*
drawopen(Chan *c, int omode)
{
	Client *cl;

	if(c->qid.path & CHDIR)
		return devopen(c, omode, 0, 0, drawgen);

	qlock(&sdraw);
	if(waserror()){
		qunlock(&sdraw);
		nexterror();
	}

	if(QID(c->qid) == Qnew){
		cl = drawnewclient();
		if(cl == 0)
			error(Enodev);
		c->qid.path = Qctl|((cl->slot+1)<<QSHIFT);
	}

	switch(QID(c->qid)){
	case Qnew:
		break;

	case Qctl:
		cl = drawclient(c);
		if(cl->busy)
			error(Einuse);
		cl->busy = 1;
		flushrect = Rect(10000, 10000, -10000, -10000);
		drawinstall(cl, 0, &screenimage, 0);
		incref(&cl->r);
		break;
	case Qcolormap:
	case Qdata:
	case Qrefresh:
		cl = drawclient(c);
		incref(&cl->r);
		break;
	}
	qunlock(&sdraw);
	poperror();
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
drawclose(Chan *c)
{
	int i;
	DImage *d, **dp;
	Client *cl;
	Refresh *r;

	if(c->qid.path & CHDIR)
		return;
	qlock(&sdraw);
	if(waserror()){
		qunlock(&sdraw);
		nexterror();
	}

	cl = drawclient(c);
	if(QID(c->qid) == Qctl)
		cl->busy = 0;
	if((c->flag&COPEN) && (decref(&cl->r)==0)){
		while(r = cl->refresh){	/* assign = */
			cl->refresh = r->next;
			free(r);
		}
		/* free names */
		for(i=0; i<sdraw.nname; )
			if(sdraw.name[i].client == cl)
				drawdelname(sdraw.name+i);
			else
				i++;
		while(cl->cscreen)
			drawuninstallscreen(cl, cl->cscreen);
		/* all screens are freed, so now we can free images */
		dp = cl->dimage;
		for(i=0; i<NHASH; i++){
			while(d = *dp){	/* assign = */
				*dp = d->next;
				drawfreedimage(d);
			}
			dp++;
		}
		sdraw.client[cl->slot] = 0;
		drawflush();	/* to erase visible, now dead windows */
		free(cl);
	}
	qunlock(&sdraw);
	poperror();
}

long
drawread(Chan *c, void *a, long n, ulong offset)
{
	int index, m;
	ulong red, green, blue;
	Client *cl;
	uchar *p;
	Refresh *r;
	DImage *di;
	Memimage *i;

	USED(offset);
	if(c->qid.path & CHDIR)
		return devdirread(c, a, n, 0, 0, drawgen);
	cl = drawclient(c);
	qlock(&sdraw);
	if(waserror()){
		qunlock(&sdraw);
		nexterror();
	}
	switch(QID(c->qid)){
	case Qctl:
		if(n <= 12*12)
			error(Eshortread);
		if(cl->infoid < 0)
			error(Enodrawimage);
		if(cl->infoid == 0)
			i = &screenimage;
		else{
			di = drawlookup(cl, cl->infoid, 1);
			if(di == nil)
				error(Enodrawimage);
			i = di->image;
		}
		n = sprint(a, "%11d %11d %11d %11d %11d %11d %11d %11d %11d %11d %11d %11d ",
			cl->clientid, cl->infoid, i->ldepth, i->repl,
			i->r.min.x, i->r.min.y, i->r.max.x, i->r.max.y,
			i->clipr.min.x, i->clipr.min.y, i->clipr.max.x, i->clipr.max.y);
		cl->infoid = -1;
		break;

	case Qcolormap:
		drawactive(1);	/* to restore map from backup */
		p = malloc(4*12*256);
		if(p == 0)
			error(Enomem);
		m = 0;
		for(index = 0; index < 256; index++){
			getcolor(index, &red, &green, &blue);
			m += sprint((char*)p+m, "%11d %11d %11d %11d\n", index, red>>24, green>>24, blue>>24);
		}
		n = readstr(offset, a, n, (char*)p);
		free(p);
		break;

	case Qdata:
		if(cl->readdata == nil)
			error("no draw data");
		if(n < cl->nreaddata)
			error(Eshortread);
		n = cl->nreaddata;
		memmove(a, cl->readdata, cl->nreaddata);
		free(cl->readdata);
		cl->readdata = nil;
		break;

	case Qrefresh:
		if(n < 5*4)
			error(Ebadarg);
		for(;;){
			if(cl->refreshme || cl->refresh)
				break;
			qunlock(&sdraw);
			sleep(&cl->refrend, drawrefactive, cl);
			qlock(&sdraw);
		}
		p = a;
		while(cl->refresh && n>=5*4){
			r = cl->refresh;
			BPLONG(p+0*4, r->dimage->id);
			BPLONG(p+1*4, r->r.min.x);
			BPLONG(p+2*4, r->r.min.y);
			BPLONG(p+3*4, r->r.max.x);
			BPLONG(p+4*4, r->r.max.y);
			cl->refresh = r->next;
			free(r);
			p += 5*4;
			n -= 5*4;
		}
		cl->refreshme = 0;
		n = p-(uchar*)a;
	}
	qunlock(&sdraw);
	poperror();
	return n;
}

void
drawwakeall(void)
{
	Client *cl;
	int i;

	for(i=0; i<sdraw.nclient; i++){
		cl = sdraw.client[i];
		if(cl && (cl->refreshme || cl->refresh))
			wakeup(&cl->refrend);
	}
}

static long
drawwrite(Chan *c, void *a, long n, ulong offset)
{
	char buf[128], *fields[4], *q;
	Client *cl;
	int i, m, red, green, blue, x;

	USED(offset);
	if(c->qid.path & CHDIR)
		error(Eisdir);
	cl = drawclient(c);
	qlock(&sdraw);
	if(waserror()){
		drawwakeall();
		qunlock(&sdraw);
		nexterror();
	}
	switch(QID(c->qid)){
	case Qctl:
		if(n != 4)
			error("unknown draw control request");
		cl->infoid = BGLONG((uchar*)a);
		break;

	case Qcolormap:
		drawactive(1);	/* to restore map from backup */
		m = n;
		n = 0;
		while(m > 0){
			x = m;
			if(x > sizeof(buf)-1)
				x = sizeof(buf)-1;
			q = memccpy(buf, a, '\n', x);
			if(q == 0)
				break;
			i = q-buf;
			n += i;
			a = (char*)a + i;
			m -= i;
			*q = 0;
			if(parsefields(buf, fields, nelem(fields), " ") != 4)
				error(Ebadarg);
			i = strtoul(fields[0], 0, 0);
			red = strtoul(fields[1], 0, 0);
			green = strtoul(fields[2], 0, 0);
			blue = strtoul(fields[3], &q, 0);
			if(fields[3] == q)
				error(Ebadarg);
			if(red>255 || green>255 || blue>255 || i<0 || i>255)
				error(Ebadarg);
			red |= red<<8;
			red |= red<<16;
			green |= green<<8;
			green |= green<<16;
			blue |= blue<<8;
			blue |= blue<<16;
			setcolor(i, red, green, blue);
		}
		break;

	case Qdata:
		drawmesg(cl, a, n);
		drawwakeall();
		break;

	default:
		error(Ebadusefd);
	}
	qunlock(&sdraw);
	poperror();
	return n;
}

uchar*
drawcoord(uchar *p, uchar *maxp, int oldx, int *newx)
{
	int b, x;

	if(p >= maxp)
		error(Eshortdraw);
	b = *p++;
	x = b & 0x7F;
	if(b & 0x80){
		if(p+1 >= maxp)
			error(Eshortdraw);
		x |= *p++ << 7;
		x |= *p++ << 15;
		if(x & (1<<22))
			x |= ~0<<23;
	}else{
		if(b & 0x40)
			x |= ~0<<7;
		x += oldx;
	}
	*newx = x;
	return p;
}

void
drawmesg(Client *client, void *av, int n)
{
	int c, ldepth, repl, m, y, dstid, scrnid, ni, ci, j, nw, e0, e1, ox, oy, esize, doflush;
	uchar *u, *a;
	Rectangle r, clipr;
	Point p, q, *pp, sp;
	Memimage *i, *dst, *src, *mask;
	Memimage *l, **lp;
	Memscreen *scrn;
	DImage *font, *ll, *di, *ddst, *dsrc;
	DName *dn;
	DScreen *dscrn;
	FChar *fc;
	Refx *refx;
	CScreen *cs;
	Refreshfn reffn;

	a = av;
	m = 0;
	while((n-=m) > 0){
		a += m;
		switch(*a){
		default:
			error("bad draw command");
		/* allocate: 'a' id[4] screenid[4] refresh[1] ldepth[2] repl[1] R[4*4] clipR[4*4] value[1] */
		case 'a':
			m = 1+4+4+1+2+1+4*4+4*4+1;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			if(drawlookup(client, dstid, 0))
				error(Eimageexists);
			ldepth = BGSHORT(a+10);
			repl = a[12];
			drawrectangle(&r, a+13);
			scrnid = BGSHORT(a+5);
			if(scrnid){
				dscrn = drawlookupscreen(client, scrnid, &cs);
				scrn = dscrn->screen;
				if(repl || ldepth!=scrn->image->ldepth)
					error("image parameters incompatible with screen");
				reffn = nil;
				switch(a[9]){
				case Refbackup:
					break;
				case Refnone:
					reffn = memlnorefresh;
					break;
				case Refmesg:
					reffn = drawrefresh;
					break;
				default:
					error("unknown refresh method");
				}
				l = memlalloc(scrn, r, reffn, 0, a[45]);
				if(l == 0)
					error(Edrawmem);
				addflush(l->layer->screenr);
				drawrectangle(&l->clipr, a+29);
				rectclip(&l->clipr, r);
				if(drawinstall(client, dstid, l, dscrn) == 0){
					memldelete(l);
					error(Edrawmem);
				}
				dscrn->ref++;
				if(reffn){
					refx = nil;
					if(reffn == drawrefresh){
						refx = malloc(sizeof(Refx));
						if(refx == 0){
							drawuninstall(client, dstid);
							error(Edrawmem);
						}
						refx->client = client;
						refx->dimage = drawlookup(client, dstid, 1);
					}
					memlsetrefresh(l, reffn, refx);
				}
				continue;
			}
			i = allocmemimage(r, ldepth);
			if(i == 0)
				error(Edrawmem);
			i->repl = repl;
			drawrectangle(&i->clipr, a+29);
			if(!repl)
				rectclip(&i->clipr, r);
			if(drawinstall(client, dstid, i, 0) == 0){
				freememimage(i);
				error(Edrawmem);
			}
			memfillcolor(i, a[45]);
			continue;

		/* allocate screen: 'A' id[4] imageid[4] fillid[4] public[1] */
		case 'A':
			m = 1+4+4+4+1;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			if(dstid == 0)
				error(Ebadarg);
			if(drawlookupdscreen(dstid))
				error(Escreenexists);
			ddst = drawlookup(client, BGLONG(a+5), 1);
			dsrc = drawlookup(client, BGLONG(a+9), 1);
			if(ddst==0 || dsrc==0)
				error(Enodrawimage);
			if(drawinstallscreen(client, 0, dstid, ddst, dsrc, a[13]) == 0)
				error(Edrawmem);
			continue;

		/* set repl and clip: 'c' dstid[4] repl[1] clipR[4*4] */
		case 'c':
			m = 1+4+1+4*4;
			if(n < m)
				error(Eshortdraw);
			ddst = drawlookup(client, BGLONG(a+1), 1);
			if(ddst == nil)
				error(Enodrawimage);
			if(ddst->name)
				error("can't change repl/clipr of shared image");
			dst = ddst->image;
			dst->repl = a[5];
			drawrectangle(&dst->clipr, a+6);
			continue;

		/* draw: 'd' dstid[4] srcid[4] maskid[4] R[4*4] P[2*4] P[2*4] */
		case 'd':
			m = 1+4+4+4+4*4+2*4+2*4;
			if(n < m)
				error(Eshortdraw);
			dst = drawimage(client, a+1);
			dstid = BGLONG(a+1);
			src = drawimage(client, a+5);
			mask = drawimage(client, a+9);
			drawrectangle(&r, a+13);
			drawpoint(&p, a+29);
			drawpoint(&q, a+37);
			memdraw(dst, r, src, p, mask, q);
			dstflush(dstid, dst, r);
			continue;

		/* ellipse: 'e' dstid[4] srcid[4] center[2*4] a[4] b[4] thick[4] sp[2*4] alpha[4] phi[4]*/
		case 'e':
		case 'E':
			m = 1+4+4+2*4+4+4+4+2*4+2*4;
			if(n < m)
				error(Eshortdraw);
			dst = drawimage(client, a+1);
			dstid = BGLONG(a+1);
			src = drawimage(client, a+5);
			drawpoint(&p, a+9);
			e0 = BGLONG(a+17);
			e1 = BGLONG(a+21);
			if(e0<0 || e1<0)
				error("invalid ellipse semidiameter");
			j = BGLONG(a+25);
			if(j < 0)
				error("negative ellipse thickness");
			drawpoint(&sp, a+29);
			c = j;
			if(*a == 'E')
				c = -1;
			ox = BGLONG(a+37);
			oy = BGLONG(a+41);
			/* high bit indicates arc angles are present */
			if(ox & (1<<31)){
				if((ox & (1<<30)) == 0)
					ox &= ~(1<<31);
				memarc(dst, p, e0, e1, c, src, sp, ox, oy);
			}else
				memellipse(dst, p, e0, e1, c, src, sp);
			dstflush(dstid, dst, Rect(p.x-e0-j, p.y-e1-j, p.x+e0+j+1, p.y+e1+j+1));
			continue;

		/* free: 'f' id[4] */
		case 'f':
			m = 1+4;
			if(n < m)
				error(Eshortdraw);
			ll = drawlookup(client, BGLONG(a+1), 0);
			if(ll && ll->dscreen && ll->dscreen->owner != client)
				ll->dscreen->owner->refreshme = 1;
			drawuninstall(client, BGLONG(a+1));
			continue;

		/* free screen: 'F' id[4] */
		case 'F':
			m = 1+4;
			if(n < m)
				error(Eshortdraw);
			drawlookupscreen(client, BGLONG(a+1), &cs);
			drawuninstallscreen(client, cs);
			continue;

		/* initialize font: 'i' fontid[4] nchars[4] ascent[1] */
		case 'i':
			m = 1+4+4+1;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			if(dstid == 0)
				error("can't use display as font");
			font = drawlookup(client, dstid, 1);
			if(font == 0)
				error(Enodrawimage);
			if(font->image->layer)
				error("can't use window as font");
			free(font->fchar);	/* should we complain if non-zero? */
			ni = BGLONG(a+5);
			font->fchar = malloc(ni*sizeof(FChar));
			if(font->fchar == 0)
				error("no memory for font");
			memset(font->fchar, 0, ni*sizeof(FChar));
			font->nfchar = ni;
			font->ascent = a[9];
			continue;

		/* load character: 'l' fontid[4] srcid[4] index[2] R[4*4] P[2*4] left[1] width[1] */
		case 'l':
			m = 1+4+4+2+4*4+2*4+1+1;
			if(n < m)
				error(Eshortdraw);
			font = drawlookup(client, BGLONG(a+1), 1);
			if(font == 0)
				error(Enodrawimage);
			if(font->nfchar == 0)
				error(Enotfont);
			src = drawimage(client, a+5);
			ci = BGSHORT(a+9);
			if(ci >= font->nfchar)
				error(Eindex);
			drawrectangle(&r, a+11);
			drawpoint(&p, a+27);
			memdraw(font->image, r, src, p, memones, p);
			fc = &font->fchar[ci];
			fc->minx = r.min.x;
			fc->maxx = r.max.x;
			fc->miny = r.min.y;
			fc->maxy = r.max.y;
			fc->left = a[35];
			fc->width = a[36];
			continue;

		/* draw line: 'L' dstid[4] p0[2*4] p1[2*4] end0[4] end1[4] radius[4] srcid[4] sp[2*4] */
		case 'L':
			m = 1+4+2*4+2*4+4+4+4+4+2*4;
			if(n < m)
				error(Eshortdraw);
			dst = drawimage(client, a+1);
			dstid = BGLONG(a+1);
			drawpoint(&p, a+5);
			drawpoint(&q, a+13);
			e0 = BGLONG(a+21);
			e1 = BGLONG(a+25);
			j = BGLONG(a+29);
			if(j < 0)
				error("negative line width");
			src = drawimage(client, a+33);
			drawpoint(&sp, a+37);
			memline(dst, p, q, e0, e1, j, src, sp);
			/* avoid memlinebbox if possible */
			if(dstid==0 || dst->layer!=nil){
				/* BUG: this is terribly inefficient: update maximal containing rect*/
				r = memlinebbox(p, q, e0, e1, j);
				dstflush(dstid, dst, insetrect(r, -(1+1+j)));
			}
			continue;

		/* attach to a named image: 'n' dstid[4] j[1] name[j] */
		case 'n':
			m = 1+4+1;
			if(n < m)
				error(Eshortdraw);
			j = a[5];
			if(j == 0)	/* give me a non-empty name please */
				error(Eshortdraw);
			m += j;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			if(drawlookup(client, dstid, 0))
				error(Eimageexists);
			dn = drawlookupname(j, (char*)a+6);
			if(dn == nil)
				error(Enoname);
			if(drawinstall(client, dstid, dn->dimage->image, 0) == 0)
				error(Edrawmem);
			di = drawlookup(client, dstid, 0);
			if(di == 0)
				error("draw: can't happen");
			di->vers = dn->vers;
			di->name = smalloc(j+1);
			memmove(di->name, a+6, j);
			di->name[j] = 0;
			client->infoid = dstid;
			continue;

		/* name an image: 'N' dstid[4] in[1] j[1] name[j] */
		case 'N':
			m = 1+4+1+1;
			if(n < m)
				error(Eshortdraw);
			c = a[5];
			j = a[6];
			if(j == 0)	/* give me a non-empty name please */
				error(Eshortdraw);
			m += j;
			if(n < m)
				error(Eshortdraw);
			di = drawlookup(client, BGLONG(a+1), 0);
			if(di == 0)
				error(Enodrawimage);
			if(di->name)
				error(Enamed);
			if(c)
				drawaddname(client, di, j, (char*)a+7);
			else{
				dn = drawlookupname(j, (char*)a+7);
				if(dn == nil)
					error(Enoname);
				if(dn->dimage != di)
					error(Ewrongname);
				drawdelname(dn);
			}
			continue;

		/* position window: 'o' id[4] r.min [2*4] screenr.min [2*4] */
		case 'o':
			m = 1+4+2*4+2*4;
			if(n < m)
				error(Eshortdraw);
			dst = drawimage(client, a+1);
			if(dst->layer){
				drawpoint(&p, a+5);
				drawpoint(&q, a+13);
				r = dst->layer->screenr;
				ni = memlorigin(dst, p, q);
				if(ni < 0)
					error("image origin failed");
				if(ni > 0){
					addflush(r);
					addflush(dst->layer->screenr);
					ll = drawlookup(client, BGLONG(a+1), 1);
					if(ll->dscreen->owner != client)
						ll->dscreen->owner->refreshme = 1;
				}
			}
			continue;

		/* filled polygon: 'P' dstid[4] n[2] wind[4] ignore[2*4] srcid[4] sp[2*4] p0[2*4] dp[2*2*n] */
		/* polygon: 'p' dstid[4] n[2] end0[4] end1[4] radius[4] srcid[4] sp[2*4] p0[2*4] dp[2*2*n] */
		case 'p':
		case 'P':
			m = 1+4+2+4+4+4+4+2*4;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			dst = drawimage(client, a+1);
			ni = BGSHORT(a+5);
			if(ni < 0)
				error("negative count in polygon");
			e0 = BGLONG(a+7);
			e1 = BGLONG(a+11);
			j = 0;
			if(*a == 'p'){
				j = BGLONG(a+15);
				if(j < 0)
					error("negative polygon line width");
			}
			src = drawimage(client, a+19);
			drawpoint(&sp, a+23);
			drawpoint(&p, a+31);
			ni++;
			pp = malloc(ni*sizeof(Point));
			if(pp == nil)
				error(Enomem);
			doflush = 0;
			if(dstid==0 || (dst->layer && dst->layer->screen->image->data == screenimage.data))
				doflush = 1;	/* simplify test in loop */
			ox = oy = 0;
			u = a+m;
			for(y=0; y<ni; y++){
				u = drawcoord(u, a+n, ox, &p.x);
				u = drawcoord(u, a+n, oy, &p.y);
				ox = p.x;
				oy = p.y;
				if(doflush){
					esize = j;
					if(*a == 'p'){
						if(y == 0){
							c = memlineendsize(e0);
							if(c > esize)
								esize = c;
						}
						if(y == ni-1){
							c = memlineendsize(e1);
							if(c > esize)
								esize = c;
						}
					}
					if(*a=='P' && e0!=1 && e0 !=~0)
						r = dst->clipr;
					else
						r = Rect(p.x-esize, p.y-esize, p.x+esize+1, p.y+esize+1);
					dstflush(dstid, dst, r);
				}
				pp[y] = p;
			}
			if(*a == 'p')
				mempoly(dst, pp, ni, e0, e1, j, src, sp);
			else
				memfillpoly(dst, pp, ni, e0, src, sp);
			free(pp);
			m = u-a;
			continue;

		/* read: 'r' id[4] R[4*4] */
		case 'r':
			m = 1+4+4*4;
			if(n < m)
				error(Eshortdraw);
			i = drawimage(client, a+1);
			if(i->layer)
				error("readimage from window unimplemented");
			drawrectangle(&r, a+5);
			if(!rectinrect(r, i->r))
				error(Ereadoutside);
			c = bytesperline(r, i->ldepth);
			c *= Dy(r);
			free(client->readdata);
			client->readdata = mallocz(c, 0);
			if(client->readdata == nil)
				error("readimage malloc failed");
			client->nreaddata = unloadmemimage(i, r, client->readdata, c);
			if(client->nreaddata < 0){
				free(client->readdata);
				client->readdata = nil;
				error("bad readimage call");
			}
			continue;

		/* string: 's' dstid[4] srcid[4] fontid[4] P[2*4] clipr[4*4] sp[2*4] ni[2] ni*(index[2]) */
		/* stringbg: 'x' dstid[4] srcid[4] fontid[4] P[2*4] clipr[4*4] sp[2*4] ni[2] bgid[4] bgpt[2*4] ni*(index[2]) */
		case 's':
		case 'x':
			m = 1+4+4+4+2*4+4*4+2*4+2;
			if(*a == 'x')
				m += 4+2*4;
			if(n < m)
				error(Eshortdraw);

		CaseS:
			dst = drawimage(client, a+1);
			dstid = BGLONG(a+1);
			src = drawimage(client, a+5);
			font = drawlookup(client, BGLONG(a+9), 1);
			if(font == 0)
				error(Enodrawimage);
			if(font->nfchar == 0)
				error(Enotfont);
			drawpoint(&p, a+13);
			drawrectangle(&r, a+21);
			drawpoint(&sp, a+37);
			ni = BGSHORT(a+45);
			u = a+m;
			m += ni*2;
			if(n < m)
				error(Eshortdraw);
			clipr = dst->clipr;
			dst->clipr = r;
			if(*a == 'x'){
				/* paint background */
				l = drawimage(client, a+47);
				drawpoint(&q, a+51);
				r.min.x = p.x;
				r.min.y = p.y-font->ascent;
				r.max.x = p.x;
				r.max.y = r.min.y+Dy(font->image->r);
				j = ni;
				while(--j >= 0){
					ci = BGSHORT(u);
					if(ci<0 || ci>=font->nfchar){
						dst->clipr = clipr;
						error(Eindex);
					}
					r.max.x += font->fchar[ci].width;
					u += 2;
				}
				memdraw(dst, r, l, q, memones, ZP);
				u -= 2*ni;
			}
			q = p;
			while(--ni >= 0){
				ci = BGSHORT(u);
				if(ci<0 || ci>=font->nfchar){
					dst->clipr = clipr;
					error(Eindex);
				}
				q = drawchar(dst, q, src, &sp, font, ci);
				u += 2;
			}
			dst->clipr = clipr;
			p.y -= font->ascent;
			dstflush(dstid, dst, Rect(p.x, p.y, q.x, p.y+Dy(font->image->r)));
			continue;

		/* use public screen: 'S' id[4] ldepth[4] */
		case 'S':
			m = 1+4+4;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			if(dstid == 0)
				error(Ebadarg);
			dscrn = drawlookupdscreen(dstid);
			if(dscrn==0 || (dscrn->public==0 && dscrn->owner!=client))
				error(Enodrawscreen);
			if(dscrn->screen->image->ldepth != BGLONG(a+5))
				error("inconsistent ldepth");
			if(drawinstallscreen(client, dscrn, 0, 0, 0, 0) == 0)
				error(Edrawmem);
			continue;

		/* top or bottom windows: 't' top[1] nw[2] n*id[4] */
		case 't':
			m = 1+1+2;
			if(n < m)
				error(Eshortdraw);
			nw = BGSHORT(a+2);
			if(nw < 0)
				error(Ebadarg);
			if(nw == 0)
				continue;
			m += nw*4;
			if(n < m)
				error(Eshortdraw);
			lp = malloc(nw*sizeof(Memimage*));
			if(lp == 0)
				error(Enomem);
			if(waserror()){
				free(lp);
				nexterror();
			}
			for(j=0; j<nw; j++)
				lp[j] = drawimage(client, a+1+1+2+j*4);
			if(lp[0]->layer == 0)
				error("images are not windows");
			for(j=1; j<nw; j++)
				if(lp[j]->layer->screen != lp[0]->layer->screen)
					error("images not on same screen");
			if(a[1])
				memltofrontn(lp, nw);
			else
				memltorearn(lp, nw);
			if(lp[0]->layer->screen->image->data == screenimage.data)
				for(j=0; j<nw; j++)
					addflush(lp[j]->layer->screenr);
			ll = drawlookup(client, BGLONG(a+1+1+2), 1);
			if(ll->dscreen->owner != client)
				ll->dscreen->owner->refreshme = 1;
			poperror();
			free(lp);
			continue;

		/* visible: 'v' */
		case 'v':
			m = 1;
			drawflush();
			continue;

		/* write: 'w' id[4] R[4*4] data[x*1] */
		/* write from compressed data: 'W' id[4] R[4*4] data[x*1] */
		case 'w':
		case 'W':
			m = 1+4+4*4;
			if(n < m)
				error(Eshortdraw);
			dstid = BGLONG(a+1);
			dst = drawimage(client, a+1);
			drawrectangle(&r, a+5);
			if(!rectinrect(r, dst->r))
				error(Ewriteoutside);
			y = memload(dst, r, a+m, n-m, *a=='W');
			if(y < 0)
				error("bad writeimage call");
			dstflush(dstid, dst, r);
			m += y;
			continue;
		}
	}
}

Dev drawdevtab = {
	'i',
	"draw",

	devreset,
	devinit,
	drawattach,
	devclone,
	drawwalk,
	drawstat,
	drawopen,
	devcreate,
	drawclose,
	drawread,
	devbread,
	drawwrite,
	devbwrite,
	devremove,
	devwstat,
};

/*
 * On 8 bit displays, load the default color map
 */
void
drawcmap(invert){
	int r, g, b, cr, cg, cb, v;
	int num, den;
	int i, j;
	drawactive(1);	/* to restore map from backup */
	for(r=0,i=0;r!=4;r++) for(v=0;v!=4;v++,i+=16){
		for(g=0,j=v-r;g!=4;g++) for(b=0;b!=4;b++,j++){
			den=r;
			if(g>den) den=g;
			if(b>den) den=b;
			if(den==0)	/* divide check -- pick grey shades */
				cr=cg=cb=v*17;
			else{
				num=17*(4*den+v);
				cr=r*num/den;
				cg=g*num/den;
				cb=b*num/den;
			}
			if(invert)
				setcolor(255-i-(j&15),
					cr*0x01010101, cg*0x01010101, cb*0x01010101);
			else
				setcolor(i+(j&15),
					cr*0x01010101, cg*0x01010101, cb*0x01010101);
		}
	}
}

void
drawblankscreen(int blank)
{
	int i, nc;
	ulong *p;

	if(blank == sdraw.blanked)
		return;
	p = sdraw.savemap;
	nc = 1<<(1<<screenimage.ldepth);
	if(blank == 0)	/* turn screen on */
		for(i=0; i<nc; i++, p+=3)
			setcolor(i, p[0], p[1], p[2]);
	else	/* turn screen off */
		for(i=0; i<nc; i++, p+=3){
			getcolor(i, &p[0], &p[1], &p[2]);
			setcolor(i, 0, 0, 0);
		}
	sdraw.blanked = blank;
}

/*
 * record activity on screen, changing blanking as appropriate
 */
void
drawactive(int active)
{
	if(active){
		drawblankscreen(0);
		sdraw.blanktime = 0;
	}else {
		if(TK2MS(sdraw.blanktime) > BLANKTIME)
			drawblankscreen(1);
		else
			sdraw.blanktime++;
	}
}