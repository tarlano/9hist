#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	"ip.h"

#define DPRINT if(0)print

enum
{
	UDP_PHDRSIZE	= 12,
	UDP_HDRSIZE	= 20,
	UDP_IPHDR	= 8,
	IP_UDPPROTO	= 17,
	UDP_USEAD	= 12,

	Udprxms		= 200,
	Udptickms	= 100,
	Udpmaxxmit	= 10,
};

typedef struct Udphdr Udphdr;
struct Udphdr
{
	/* ip header */
	byte	vihl;		/* Version and header length */
	byte	tos;		/* Type of service */
	byte	length[2];	/* packet length */
	byte	id[2];		/* Identification */
	byte	frag[2];	/* Fragment information */
	byte	Unused;	
	byte	udpproto;	/* Protocol */
	byte	udpplen[2];	/* Header plus data length */
	byte	udpsrc[4];	/* Ip source */
	byte	udpdst[4];	/* Ip destination */

	/* udp header */
	byte	udpsport[2];	/* Source port */
	byte	udpdport[2];	/* Destination port */
	byte	udplen[2];	/* data length */
	byte	udpcksum[2];	/* Checksum */
};

/*
 *  protocol specific part of Conv
 */
typedef struct Udpcb Udpcb;
struct Udpcb
{
	QLock;
	byte	headers;
};

	Proto	udp;
	int	udpsum;
	uint	generation;
extern	Fs	fs;
	int	udpdebug;

static char*
udpconnect(Conv *c, char **argv, int argc)
{
	char *e;

	e = Fsstdconnect(c, argv, argc);
	Fsconnected(&fs, c, e);

	return e;
}

int
udpstate(char **s, Conv *c)
{
	USED(c);
	*s = "Datagram";
	return 1;
}

static char*
udpannounce(Conv *c, char** argv, int argc)
{
	char *e;

	e = Fsstdannounce(c, argv, argc);
	if(e != nil)
		return e;
	Fsconnected(&fs, c, nil);

	return nil;
}

static void
udpcreate(Conv *c)
{
	c->rq = qopen(16*1024, 1, 0, 0);
	c->wq = qopen(16*1024, 0, 0, 0);
}

static void
udpclose(Conv *c)
{
	Udpcb *ucb;

	qclose(c->rq);
	qclose(c->wq);
	qclose(c->eq);
	c->laddr = 0;
	c->raddr = 0;
	c->lport = 0;
	c->rport = 0;

	ucb = (Udpcb*)c->ptcl;
	ucb->headers = 0;

	unlock(c);
}

void
udpkick(Conv *c, int l)
{
	Udphdr *uh;
	ushort rport;
	Ipaddr laddr, raddr;
	Block *bp, *f;
	Udpcb *ucb;
	int dlen, ptcllen;

	USED(l);

	bp = qget(c->wq);
	if(bp == nil)
		return;

	ucb = (Udpcb*)c->ptcl;
	if(ucb->headers) {
		/* get user specified addresses */
		bp = pullupblock(bp, UDP_USEAD);
		if(bp == nil) {
			freeblist(bp);
			return;
		}
		raddr = nhgetl(bp->rp);
		bp->rp += 4;
		laddr = nhgetl(bp->rp);
		if(laddr != 0 && Mediaforme(bp->rp) <= 0)
			laddr = 0;
		bp->rp += 4;
		rport = nhgets(bp->rp);
		bp->rp += 2;
		/* ignore local port number */
		bp->rp += 2;
	} else {
		raddr = 0;
		rport = 0;
		laddr = 0;
	}

	/* Round packet up to even number of bytes */
	dlen = blocklen(bp);
	if(dlen & 1) {
		for(f = bp; f->next; f = f->next)
			;
		if(f->wp >= f->lim) {
			f->next = allocb(1);
			f = f->next;
		}
		*f->wp++ = 0;
		dlen++;	
	}

	/* Make space to fit udp & ip header */
	bp = padblock(bp, UDP_IPHDR+UDP_HDRSIZE);
	if(bp == nil)
		return;

	uh = (Udphdr *)(bp->rp);

	ptcllen = dlen + (UDP_HDRSIZE-UDP_PHDRSIZE);
	uh->Unused = 0;
	uh->udpproto = IP_UDPPROTO;
	uh->frag[0] = 0;
	uh->frag[1] = 0;
	hnputs(uh->udpplen, ptcllen);
	if(ucb->headers) {
		hnputl(uh->udpdst, raddr);
		hnputs(uh->udpdport, rport);
		if(laddr)
			hnputl(uh->udpsrc, laddr);
		else
			hnputl(uh->udpsrc, Mediagetsrc(uh->udpdst));
	} else {
		hnputl(uh->udpdst, c->raddr);
		hnputs(uh->udpdport, c->rport);
		hnputl(uh->udpsrc, c->laddr);
	}
	hnputs(uh->udpsport, c->lport);
	hnputs(uh->udplen, ptcllen);
	uh->udpcksum[0] = 0;
	uh->udpcksum[1] = 0;

	hnputs(uh->udpcksum, ptclcsum(bp, UDP_IPHDR, dlen+UDP_HDRSIZE));

	ipoput(bp, 0, c->ttl);
}

void
udpiput(Media *m, Block *bp)
{
	int len;
	Udphdr *uh;
	Conv *c, **p;
	Udpcb *ucb;
	Ipaddr raddr, laddr;
	ushort rport, lport;

	USED(m);
	uh = (Udphdr*)(bp->rp);

	/* Put back pseudo header for checksum */
	uh->Unused = 0;
	len = nhgets(uh->udplen);
	hnputs(uh->udpplen, len);

	raddr = nhgetl(uh->udpsrc);
	laddr = nhgetl(uh->udpdst);

	if(udpsum && nhgets(uh->udpcksum)) {
		if(ptclcsum(bp, UDP_IPHDR, len+UDP_PHDRSIZE)) {
			udp.csumerr++;
			netlog(Logudp, "udp: checksum error %I\n", uh->udpsrc);
			freeblist(bp);
			return;
		}
	}

	lport = nhgets(uh->udpdport);
	rport = nhgets(uh->udpsport);

	/* Look for a conversation structure for this port */
	c = nil;
	for(p = udp.conv; *p; p++) {
		c = *p;
		if(c->inuse == 0)
			continue;
		if(c->lport == lport && (c->rport == 0 || c->rport == rport))
			break;
	}

	if(*p == nil) {
		freeblist(bp);
		return;
	}

	/*
	 * Trim the packet down to data size
	 */
	len -= (UDP_HDRSIZE-UDP_PHDRSIZE);
	bp = trimblock(bp, UDP_IPHDR+UDP_HDRSIZE, len);
	if(bp == nil){
		udp.lenerr++;
		return;
	}

	ucb = (Udpcb*)c->ptcl;

	if(ucb->headers) {
		/* pass the src address */
		bp = padblock(bp, UDP_USEAD);
		hnputl(bp->rp, raddr);
		if(Mediaforme(uh->udpdst) > 0)
			hnputl(bp->rp+4, laddr);
		else
			hnputl(bp->rp+4, m->myip[0]);
		hnputs(bp->rp+8, rport);
		hnputs(bp->rp+10, lport);
	} else {
		/* connection oriented udp */
		if(c->raddr == 0){
			/* save the src address in the conversation */
		 	c->raddr = raddr;
			c->rport = rport;

			/* reply with the same ip address (if not broadcast) */
			if(Mediaforme(uh->udpdst) > 0)
				c->laddr = laddr;
			else
				c->laddr = m->myip[0];
		}
	}
	if(bp->next)
		bp = concatblock(bp);

	if(qfull(c->rq))
		freeblist(bp);
	else
		qpass(c->rq, bp);
}

char*
udpctl(Conv *c, char **f, int n)
{
	Udpcb *ucb;

	ucb = (Udpcb*)c->ptcl;
	if(n == 1 && strcmp(f[0], "headers") == 0){
		ucb->headers = 1;
		return nil;
	}
	if(n == 1 && strcmp(f[0], "debug") == 0){
		udpdebug = 1;
		return nil;
	}
	return "unknown control request";
}

void
udpinit(Fs *fs)
{
	udp.name = "udp";
	udp.kick = udpkick;
	udp.connect = udpconnect;
	udp.announce = udpannounce;
	udp.ctl = udpctl;
	udp.state = udpstate;
	udp.create = udpcreate;
	udp.close = udpclose;
	udp.rcv = udpiput;
	udp.ipproto = IP_UDPPROTO;
	udp.nc = 16;
	udp.ptclsize = sizeof(Udpcb);

	Fsproto(fs, &udp);
}
