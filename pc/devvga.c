/*
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

enum {
	Qdir,
	Qvgactl,
};

static Dirtab vgadir[] = {
	"vgactl",	{ Qvgactl, 0 },		0,	0660,
};

static void
vgareset(void)
{
	/* reserve the 'standard' vga registers */
	if(ioalloc(0x2b0, 0x2df-0x2b0+1, 0, "vga") < 0)
		panic("vga ports already allocated"); 
	if(ioalloc(0x3c0, 0x3da-0x3c0+1, 0, "vga") < 0)
		panic("vga ports already allocated"); 
	conf.monitor = 1;
}

static Chan*
vgaattach(char* spec)
{
	if(*spec && strcmp(spec, "0"))
		error(Eio);
	return devattach('v', spec);
}

int
vgawalk(Chan* c, char* name)
{
	return devwalk(c, name, vgadir, nelem(vgadir), devgen);
}

static void
vgastat(Chan* c, char* dp)
{
	devstat(c, dp, vgadir, nelem(vgadir), devgen);
}

static Chan*
vgaopen(Chan* c, int omode)
{
	return devopen(c, omode, vgadir, nelem(vgadir), devgen);
}

static void
vgaclose(Chan*)
{
}

static void
checkport(int start, int end)
{
	/* standard vga regs are OK */
	if(start >= 0x2b0 && end <= 0x2df+1)
		return;
	if(start >= 0x3c0 && end <= 0x3da+1)
		return;

	if(iounused(start, end))
		return;
	error(Eperm);
}

static long
vgaread(Chan* c, void* a, long n, vlong off)
{
	int len;
	char *p, *s;
	VGAscr *scr;
	ulong offset = off;

	switch(c->qid.path & ~CHDIR){

	case Qdir:
		return devdirread(c, a, n, vgadir, nelem(vgadir), devgen);

	case Qvgactl:
		scr = &vgascreen[0];

		p = malloc(READSTR);
		if(waserror()){
			free(p);
			nexterror();
		}
		if(scr->dev)
			s = scr->dev->name;
		else
			s = "cga";
		len = snprint(p, READSTR, "type: %s\n", s);
		if(scr->gscreen)
			len += snprint(p+len, READSTR-len, "size: %dx%dx%d\n",
				scr->gscreen->r.max.x, scr->gscreen->r.max.y,
				scr->gscreen->depth);
		if(scr->cur)
			s = scr->cur->name;
		else
			s = "off";
		len += snprint(p+len, READSTR-len, "hwgc: %s\n", s);
		snprint(p+len, READSTR-len, "addr: 0x%lux\n", scr->aperture);

		n = readstr(offset, a, n, p);
		poperror();
		free(p);

		return n;

	default:
		error(Egreg);
		break;
	}

	return 0;
}

static void
vgactl(char* a)
{
	int align, i, n, size, x, y, z;
	char *chanstr, *field[6], *p;
	ulong chan;
	VGAscr *scr;
	extern VGAdev *vgadev[];
	extern VGAcur *vgacur[];
	Rectangle r;

	n = parsefields(a, field, nelem(field), " ");
	if(n < 1)
		error(Ebadarg);

	scr = &vgascreen[0];
	if(strcmp(field[0], "hwgc") == 0){
		if(n < 2)
			error(Ebadarg);

		/* BUG: drawinit should become a different message rather than piggybacking */
		if(scr && scr->dev && scr->dev->drawinit)
			scr->dev->drawinit(scr);

		if(strcmp(field[1], "off") == 0){
			lock(&cursor);
			if(scr->cur){
				if(scr->cur->disable)
					scr->cur->disable(scr);
				scr->cur = nil;
			}
			unlock(&cursor);
			return;
		}

		for(i = 0; vgacur[i]; i++){
			if(strcmp(field[1], vgacur[i]->name))
				continue;
			lock(&cursor);
			if(scr->cur && scr->cur->disable)
				scr->cur->disable(scr);
			scr->cur = vgacur[i];
			if(scr->cur->enable)
				scr->cur->enable(scr);
			unlock(&cursor);
			return;
		}
	}
	else if(strcmp(field[0], "type") == 0){
		if(n < 2)
			error(Ebadarg);

		for(i = 0; vgadev[i]; i++){
			if(strcmp(field[1], vgadev[i]->name))
				continue;
			if(scr->dev && scr->dev->disable)
				scr->dev->disable(scr);
			scr->dev = vgadev[i];
			if(scr->dev->enable)
				scr->dev->enable(scr);
			return;
		}
	}
	else if(strcmp(field[0], "size") == 0){
		if(n < 3)
			error(Ebadarg);
		x = strtoul(field[1], &p, 0);
		if(x == 0 || x > 2048)
			error(Ebadarg);
		if(*p)
			p++;

		y = strtoul(p, &p, 0);
		if(y == 0 || y > 2048)
			error(Ebadarg);
		if(*p)
			p++;

		z = strtoul(p, &p, 0);

		chanstr = field[2];
		if((chan = strtochan(chanstr)) == 0)
			error("bad channel");

		if(chantodepth(chan) != z)
			error("depth, channel do not match");

		cursoroff(1);
		deletescreenimage();
		if(screensize(x, y, z, chan))
			error(Egreg);
		vgascreenwin(scr);
		cursoron(1);
		return;
	}
	else if(strcmp(field[0], "actualsize") == 0){
		if(scr->gscreen == nil)
			error("set the screen size first");

		if(n < 2)
			error(Ebadarg);
		x = strtoul(field[1], &p, 0);
		if(x == 0 || x > 2048)
			error(Ebadarg);
		if(*p)
			p++;

		y = strtoul(p, nil, 0);
		if(y == 0 || y > 2048)
			error(Ebadarg);

		if(x > scr->gscreen->r.max.x || y > scr->gscreen->r.max.y)
			error("physical screen bigger than virtual");

		r = Rect(0,0,x,y);
		if(!eqrect(r, scr->gscreen->r)){
			if(scr->cur == nil || scr->cur->doespanning == 0)
				error("virtual screen not supported");
		}

		physgscreenr = r;
		return;
	}
	else if(strcmp(field[0], "palettedepth") == 0){
		if(n < 2)
			error(Ebadarg);

		x = strtoul(field[1], &p, 0);
		if(x != 8 && x != 6)
			error(Ebadarg);

		scr->palettedepth = x;
		return;
	}
	else if(strcmp(field[0], "drawinit") == 0){
		if(scr && scr->dev && scr->dev->drawinit)
			scr->dev->drawinit(scr);
		return;
	}
	else if(strcmp(field[0], "linear") == 0){
		if(n < 2)
			error(Ebadarg);

		size = strtoul(field[1], 0, 0);
		if(n < 3)
			align = 0;
		else
			align = strtoul(field[2], 0, 0);
		if(screenaperture(size, align))
			error("not enough free address space");
		return;
	}
/*	else if(strcmp(field[0], "memset") == 0){
		if(n < 4)
			error(Ebadarg);
		memset((void*)strtoul(field[1], 0, 0), atoi(field[2]), atoi(field[3]));
		return;
	}
*/
	else if(strcmp(field[0], "blank") == 0){
		if(n < 2)
			error(Ebadarg);
		drawblankscreen(atoi(field[1]));
		return;
	}
	else if(strcmp(field[0], "hwacceloff") == 0){
		scr->fill = nil;
		scr->scroll = nil;
		return;
	}

	error(Ebadarg);
}

static long
vgawrite(Chan* c, void* a, long n, vlong off)
{
	char *p;
	ulong offset = off;

	switch(c->qid.path & ~CHDIR){

	case Qdir:
		error(Eperm);

	case Qvgactl:
		if(offset || n >= READSTR)
			error(Ebadarg);
		p = malloc(READSTR);
		if(waserror()){
			free(p);
			nexterror();
		}
		memmove(p, a, n);
		p[n] = 0;
		vgactl(p);
		poperror();
		free(p);
		return n;

	default:
		error(Egreg);
		break;
	}

	return 0;
}

Dev vgadevtab = {
	'v',
	"vga",

	vgareset,
	devinit,
	vgaattach,
	devclone,
	vgawalk,
	vgastat,
	vgaopen,
	devcreate,
	vgaclose,
	vgaread,
	devbread,
	vgawrite,
	devbwrite,
	devremove,
	devwstat,
};
