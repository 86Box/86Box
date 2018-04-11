/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)tcp_subr.c	8.1 (Berkeley) 6/10/93
 * tcp_subr.c,v 1.5 1994/10/08 22:39:58 phk Exp
 */

/*
 * Changes and additions relating to SLiRP
 * Copyright (c) 1995 Danny Gasparovski.
 * 
 * Please read the file COPYRIGHT for the 
 * terms and conditions of the copyright.
 */

#define WANT_SYS_IOCTL_H
#include <stdlib.h>
#ifndef _WIN32
# include <unistd.h>
#endif
#include "slirp.h"

/* patchable/settable parameters for tcp */
int 	tcp_mssdflt = TCP_MSS;
int 	tcp_rttdflt = TCPTV_SRTTDFLT / PR_SLOWHZ;
int	tcp_do_rfc1323 = 0;	/* Don't do rfc1323 performance enhancements */
int	tcp_rcvspace;	/* You may want to change this */
int	tcp_sndspace;	/* Keep small if you have an error prone link */

/*
 * Tcp initialization
 */
void
tcp_init()
{
	tcp_iss = 1;		/* wrong */
	tcb.so_next = tcb.so_prev = &tcb;
	
	/* tcp_rcvspace = our Window we advertise to the remote */
	tcp_rcvspace = TCP_RCVSPACE;
	tcp_sndspace = TCP_SNDSPACE;
	
	/* Make sure tcp_sndspace is at least 2*MSS */
	if (tcp_sndspace < 2*(min(if_mtu, if_mru) - sizeof(struct tcpiphdr)))
		tcp_sndspace = 2*(min(if_mtu, if_mru) - sizeof(struct tcpiphdr));
}

/*
 * Create template to be used to send tcp packets on a connection.
 * Call after host entry created, fills
 * in a skeletal tcp/ip header, minimizing the amount of work
 * necessary when the connection is used.
 */
/* struct tcpiphdr * */
void
tcp_template(tp)
	struct tcpcb *tp;
{
	struct SLIRPsocket *so = tp->t_socket;
	struct tcpiphdr *n = &tp->t_template;

	n->ti_next = n->ti_prev = 0;
	n->ti_x1 = 0;
	n->ti_pr = IPPROTO_TCP;
	n->ti_len = htons(sizeof (struct tcpiphdr) - sizeof (struct ip));
	n->ti_src = so->so_faddr;
	n->ti_dst = so->so_laddr;
	n->ti_sport = so->so_fport;
	n->ti_dport = so->so_lport;
	
	n->ti_seq = 0;
	n->ti_ack = 0;
	n->ti_x2 = 0;
	n->ti_off = 5;
	n->ti_flags = 0;
	n->ti_win = 0;
	n->ti_sum = 0;
	n->ti_urp = 0;
}

/*
 * Send a single message to the TCP at address specified by
 * the given TCP/IP header.  If m == 0, then we make a copy
 * of the tcpiphdr at ti and send directly to the addressed host.
 * This is used to force keep alive messages out using the TCP
 * template for a connection tp->t_template.  If flags are given
 * then we send a message back to the TCP which originated the
 * segment ti, and discard the SLIRPmbuf containing it and any other
 * attached SLIRPmbufs.
 *
 * In any case the ack and sequence number of the transmitted
 * segment are as specified by the parameters.
 */
void
tcp_respond(tp, ti, m, ack, seq, flags)
	struct tcpcb *tp;
	struct tcpiphdr *ti;
	struct SLIRPmbuf *m;
	tcp_seq ack, seq;
	int flags;
{
	register int tlen;
	int win = 0;

	DEBUG_CALL("tcp_respond");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("ti = %lx", (long)ti);
	DEBUG_ARG("m = %lx", (long)m);
	DEBUG_ARG("ack = %u", ack);
	DEBUG_ARG("seq = %u", seq);
	DEBUG_ARG("flags = %x", flags);
	
	if (tp)
		win = sbspace(&tp->t_socket->so_rcv);
	if (m == 0) {
		if ((m = m_get()) == NULL)
			return;
#ifdef TCP_COMPAT_42
		tlen = 1;
#else
		tlen = 0;
#endif
		m->m_data += if_maxlinkhdr;
		*mtod(m, struct tcpiphdr *) = *ti;
		ti = mtod(m, struct tcpiphdr *);
		flags = TH_ACK;
	} else {
		/* 
		 * ti points into m so the next line is just making
		 * the SLIRPmbuf point to ti
		 */
		m->m_data = (SLIRPcaddr_t)ti;
		
		m->m_len = sizeof (struct tcpiphdr);
		tlen = 0;
#define xchg(a,b,type) { type t; t=a; a=b; b=t; }
		xchg(ti->ti_dst.s_addr, ti->ti_src.s_addr, u_int32_t);
		xchg(ti->ti_dport, ti->ti_sport, u_int16_t);
#undef xchg
	}
	ti->ti_len = htons((u_short)(sizeof (struct tcphdr) + tlen));
	tlen += sizeof (struct tcpiphdr);
	m->m_len = tlen;

	ti->ti_next = ti->ti_prev = 0;
	ti->ti_x1 = 0;
	ti->ti_seq = htonl(seq);
	ti->ti_ack = htonl(ack);
	ti->ti_x2 = 0;
	ti->ti_off = sizeof (struct tcphdr) >> 2;
	ti->ti_flags = flags;
	if (tp)
		ti->ti_win = htons((u_int16_t) (win >> tp->rcv_scale));
	else
		ti->ti_win = htons((u_int16_t)win);
	ti->ti_urp = 0;
	ti->ti_sum = 0;
	ti->ti_sum = cksum(m, tlen);
	((struct ip *)ti)->ip_len = tlen;

	if(flags & TH_RST) 
	  ((struct ip *)ti)->ip_ttl = MAXTTL;
	else 
	  ((struct ip *)ti)->ip_ttl = ip_defttl;
	
	(void) ip_output((struct SLIRPsocket *)0, m);
}

/*
 * Create a new TCP control block, making an
 * empty reassembly queue and hooking it to the argument
 * protocol control block.
 */
struct tcpcb *
tcp_newtcpcb(so)
	struct SLIRPsocket *so;
{
	struct tcpcb *tp;
	
	tp = (struct tcpcb *)malloc(sizeof(*tp));
	if (tp == NULL)
		return ((struct tcpcb *)0);
	
	memset((char *) tp, 0, sizeof(struct tcpcb));
	tp->seg_next = tp->seg_prev = (tcpiphdrp_32)tp;
	tp->t_maxseg = tcp_mssdflt;
	
	tp->t_flags = tcp_do_rfc1323 ? (TF_REQ_SCALE|TF_REQ_TSTMP) : 0;
	tp->t_socket = so;
	
	/*
	 * Init srtt to TCPTV_SRTTBASE (0), so we can tell that we have no
	 * rtt estimate.  Set rttvar so that srtt + 2 * rttvar gives
	 * reasonable initial retransmit time.
	 */
	tp->t_srtt = TCPTV_SRTTBASE;
	tp->t_rttvar = tcp_rttdflt * PR_SLOWHZ << 2;
	tp->t_rttmin = TCPTV_MIN;

	TCPT_RANGESET(tp->t_rxtcur, 
	    ((TCPTV_SRTTBASE >> 2) + (TCPTV_SRTTDFLT << 2)) >> 1,
	    TCPTV_MIN, TCPTV_REXMTMAX);

	tp->snd_cwnd = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->snd_ssthresh = TCP_MAXWIN << TCP_MAX_WINSHIFT;
	tp->t_state = TCPS_CLOSED;
	
	so->so_tcpcb = tp;

	return (tp);
}

/*
 * Drop a TCP connection, reporting
 * the specified error.  If connection is synchronized,
 * then send a RST to peer.
 */
struct tcpcb *tcp_drop(struct tcpcb *tp, int err) 
{
/* tcp_drop(tp, errno)
	struct tcpcb *tp;
	int errno;
{
*/

	DEBUG_CALL("tcp_drop");
	DEBUG_ARG("tp = %lx", (long)tp);
	DEBUG_ARG("errno = %d", errno);
	
	if (TCPS_HAVERCVDSYN(tp->t_state)) {
		tp->t_state = TCPS_CLOSED;
		(void) tcp_output(tp);
		tcpstat.tcps_drops++;
	} else
		tcpstat.tcps_conndrops++;
/*	if (errno == ETIMEDOUT && tp->t_softerror)
 *		errno = tp->t_softerror;
 */
/*	so->so_error = errno; */
	return (tcp_close(tp));
}

/*
 * Close a TCP control block:
 *	discard all space held by the tcp
 *	discard internet protocol block
 *	wake up any sleepers
 */
struct tcpcb *
tcp_close(tp)
	struct tcpcb *tp;
{
	struct tcpiphdr *t;
	struct SLIRPsocket *so = tp->t_socket;
	struct SLIRPmbuf *m;

	DEBUG_CALL("tcp_close");
	DEBUG_ARG("tp = %lx", (long )tp);
	
	/* free the reassembly queue, if any */
	t = (struct tcpiphdr *) tp->seg_next;
	while (t != (struct tcpiphdr *)tp) {
		t = (struct tcpiphdr *)t->ti_next;
		m = (struct SLIRPmbuf *) REASS_MBUF((struct tcpiphdr *)t->ti_prev);
		remque_32((struct tcpiphdr *) t->ti_prev);
		m_freem(m);
	}
	/* It's static */
/*	if (tp->t_template)
 *		(void) m_free(dtom(tp->t_template));
 */
/*	free(tp, M_PCB);  */
	free(tp);
	so->so_tcpcb = 0;
	soisfdisconnected(so);
	/* clobber input socket cache if we're closing the cached connection */
	if (so == tcp_last_so)
		tcp_last_so = &tcb;
	closesocket(so->s);
	sbfree(&so->so_rcv);
	sbfree(&so->so_snd);
	sofree(so);
	tcpstat.tcps_closed++;
	return ((struct tcpcb *)0);
}

void
tcp_drain()
{
	/* XXX */
}

/*
 * When a source quench is received, close congestion window
 * to one segment.  We will gradually open it again as we proceed.
 */

#ifdef notdef

void
tcp_quench(i, errno)

	int errno;
{
	struct tcpcb *tp = intotcpcb(inp);

	if (tp)
		tp->snd_cwnd = tp->t_maxseg;
}

#endif /* notdef */

/*
 * TCP protocol interface to socket abstraction.
 */

/*
 * User issued close, and wish to trail through shutdown states:
 * if never received SYN, just forget it.  If got a SYN from peer,
 * but haven't sent FIN, then go to FIN_WAIT_1 state to send peer a FIN.
 * If already got a FIN from peer, then almost done; go to LAST_ACK
 * state.  In all other cases, have already sent FIN to peer (e.g.
 * after PRU_SHUTDOWN), and just have to play tedious game waiting
 * for peer to send FIN or not respond to keep-alives, etc.
 * We can let the user exit from the close as soon as the FIN is acked.
 */
void
tcp_sockclosed(tp)
	struct tcpcb *tp;
{

	DEBUG_CALL("tcp_sockclosed");
	DEBUG_ARG("tp = %lx", (long)tp);
	
	switch (tp->t_state) {

	case TCPS_CLOSED:
	case TCPS_LISTEN:
	case TCPS_SYN_SENT:
		tp->t_state = TCPS_CLOSED;
		tp = tcp_close(tp);
		break;

	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		tp->t_state = TCPS_FIN_WAIT_1;
		break;

	case TCPS_CLOSE_WAIT:
		tp->t_state = TCPS_LAST_ACK;
		break;
	}
/*	soisfdisconnecting(tp->t_socket); */
	if (tp && tp->t_state >= TCPS_FIN_WAIT_2)
		soisfdisconnected(tp->t_socket);
	if (tp)
		tcp_output(tp);
}

/* 
 * Connect to a host on the Internet
 * Called by tcp_input
 * Only do a connect, the tcp fields will be set in tcp_input
 * return 0 if there's a result of the connect,
 * else return -1 means we're still connecting
 * The return value is almost always -1 since the socket is
 * nonblocking.  Connect returns after the SYN is sent, and does 
 * not wait for ACK+SYN.
 */
int tcp_fconnect(so)
     struct SLIRPsocket *so;
{
  int ret=0;
  
  DEBUG_CALL("tcp_fconnect");
  DEBUG_ARG("so = %lx", (long )so);

  if( (ret=so->s=socket(AF_INET,SOCK_STREAM,0)) >= 0) {
    int opt, s=so->s;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));

    fd_nonblock(s);
    opt = 1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(opt ));
    opt = 1;
    setsockopt(s,SOL_SOCKET,SO_OOBINLINE,(char *)&opt,sizeof(opt ));
    
    addr.sin_family = AF_INET;
    if ((so->so_faddr.s_addr & htonl(0xffffff00)) == special_addr.s_addr) {
      /* It's an alias */
      switch(ntohl(so->so_faddr.s_addr) & 0xff) {
      case CTL_DNS:
	addr.sin_addr = dns_addr;
	break;
      case CTL_ALIAS:
      default:
	addr.sin_addr = loopback_addr;
	break;
      }
    } else
      addr.sin_addr = so->so_faddr;
    addr.sin_port = so->so_fport;
    
    DEBUG_MISC((dfd, " connect()ing, addr.sin_port=%d, "
		"addr.sin_addr.s_addr=%.16s\n", 
		ntohs(addr.sin_port), inet_ntoa(addr.sin_addr)));
    /* We don't care what port we get */
    ret = connect(s,(struct sockaddr *)&addr,sizeof (addr));
    
    /*
     * If it's not in progress, it failed, so we just return 0,
     * without clearing SS_NOFDREF
     */
    soisfconnecting(so);
  }

  return(ret);
}

/*
 * Accept the socket and connect to the local-host
 * 
 * We have a problem. The correct thing to do would be
 * to first connect to the local-host, and only if the
 * connection is accepted, then do an accept() here.
 * But, a) we need to know who's trying to connect 
 * to the socket to be able to SYN the local-host, and
 * b) we are already connected to the foreign host by
 * the time it gets to accept(), so... We simply accept
 * here and SYN the local-host.
 */ 
void
tcp_connect(inso)
	struct SLIRPsocket *inso;
{
	struct SLIRPsocket *so;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct tcpcb *tp;
	int s, opt;

	DEBUG_CALL("tcp_connect");
	DEBUG_ARG("inso = %lx", (long)inso);
	
	/*
	 * If it's an SS_ACCEPTONCE socket, no need to socreate()
	 * another socket, just use the accept() socket.
	 */
	if (inso->so_state & SS_FACCEPTONCE) {
		/* FACCEPTONCE already have a tcpcb */
		so = inso;
	} else {
		if ((so = socreate()) == NULL) {
			/* If it failed, get rid of the pending connection */
			closesocket(accept(inso->s,(struct sockaddr *)&addr,&addrlen));
			return;
		}
		if (tcp_attach(so) < 0) {
			free(so); /* NOT sofree */
			return;
		}
		so->so_laddr = inso->so_laddr;
		so->so_lport = inso->so_lport;
	}
	
	(void) tcp_mss(sototcpcb(so), 0);

	if ((s = accept(inso->s,(struct sockaddr *)&addr,&addrlen)) < 0) {
		tcp_close(sototcpcb(so)); /* This will sofree() as well */
		return;
	}
	fd_nonblock(s);
	opt = 1;
	setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *)&opt,sizeof(int));
	opt = 1;
	setsockopt(s,SOL_SOCKET,SO_OOBINLINE,(char *)&opt,sizeof(int));
	opt = 1;
	setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(char *)&opt,sizeof(int));
	
	so->so_fport = addr.sin_port;
	so->so_faddr = addr.sin_addr;
	/* Translate connections from localhost to the real hostname */
	if (so->so_faddr.s_addr == 0 || so->so_faddr.s_addr == loopback_addr.s_addr)
	   so->so_faddr = alias_addr;
	
	/* Close the accept() socket, set right state */
	if (inso->so_state & SS_FACCEPTONCE) {
		closesocket(so->s); /* If we only accept once, close the accept() socket */
		so->so_state = SS_NOFDREF; /* Don't select it yet, even though we have an FD */
					   /* if it's not FACCEPTONCE, it's already NOFDREF */
	}
	so->s = s;
	
	so->so_iptos = tcp_tos(so);
	tp = sototcpcb(so);

	tcp_template(tp);
	
	/* Compute window scaling to request.  */
/*	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
 *		(TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.sb_hiwat)
 *		tp->request_r_scale++;
 */

/*	soisconnecting(so); */ /* NOFDREF used instead */
	tcpstat.tcps_connattempt++;
	
	tp->t_state = TCPS_SYN_SENT;
	tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
	tp->iss = tcp_iss; 
	tcp_iss += TCP_ISSINCR/2;
	tcp_sendseqinit(tp);
	tcp_output(tp);
}

/*
 * Attach a TCPCB to a socket.
 */
int
tcp_attach(so)
	struct SLIRPsocket *so;
{
	if ((so->so_tcpcb = tcp_newtcpcb(so)) == NULL)
	   return -1;
	
	insque(so, &tcb);

	return 0;
}

/*
 * Set the socket's type of service field
 */
struct tos_t tcptos[] = {
	  {0, 20, IPTOS_THROUGHPUT, 0},	/* ftp data */
	  {21, 21, IPTOS_LOWDELAY,  EMU_FTP},	/* ftp control */
	  {0, 23, IPTOS_LOWDELAY, 0},	/* telnet */
	  {0, 80, IPTOS_THROUGHPUT, 0},	/* WWW */
	  {0, 513, IPTOS_LOWDELAY, EMU_RLOGIN|EMU_NOCONNECT},	/* rlogin */
	  {0, 514, IPTOS_LOWDELAY, EMU_RSH|EMU_NOCONNECT},	/* shell */
	  {0, 544, IPTOS_LOWDELAY, EMU_KSH},		/* kshell */
	  {0, 543, IPTOS_LOWDELAY, 0},	/* klogin */
	  {0, 6667, IPTOS_THROUGHPUT, EMU_IRC},	/* IRC */
	  {0, 6668, IPTOS_THROUGHPUT, EMU_IRC},	/* IRC undernet */
	  {0, 7070, IPTOS_LOWDELAY, EMU_REALAUDIO }, /* RealAudio control */
	  {0, 113, IPTOS_LOWDELAY, EMU_IDENT }, /* identd protocol */
	  {0, 0, 0, 0}
};

struct emu_t *tcpemu = 0;
		
/*
 * Return TOS according to the above table
 */
u_int8_t
tcp_tos(so)
	struct SLIRPsocket *so;
{
	int i = 0;
	struct emu_t *emup;
	
	while(tcptos[i].tos) {
		if ((tcptos[i].fport && (ntohs(so->so_fport) == tcptos[i].fport)) ||
		    (tcptos[i].lport && (ntohs(so->so_lport) == tcptos[i].lport))) {
			so->so_emu = tcptos[i].emu;
			return tcptos[i].tos;
		}
		i++;
	}
	
	/* Nope, lets see if there's a user-added one */
	for (emup = tcpemu; emup; emup = emup->next) {
		if ((emup->fport && (ntohs(so->so_fport) == emup->fport)) ||
		    (emup->lport && (ntohs(so->so_lport) == emup->lport))) {
			so->so_emu = emup->emu;
			return emup->tos;
		}
	}
	
	return 0;
}

int do_echo = -1;

/*
 * Emulate programs that try and connect to us
 * This includes ftp (the data connection is
 * initiated by the server) and IRC (DCC CHAT and
 * DCC SEND) for now
 * 
 * NOTE: It's possible to crash SLiRP by sending it
 * unstandard strings to emulate... if this is a problem,
 * more checks are needed here
 *
 * XXX Assumes the whole command came in one packet
 *					    
 * XXX Some ftp clients will have their TOS set to
 * LOWDELAY and so Nagel will kick in.  Because of this,
 * we'll get the first letter, followed by the rest, so
 * we simply scan for ORT instead of PORT...
 * DCC doesn't have this problem because there's other stuff
 * in the packet before the DCC command.
 * 
 * Return 1 if the SLIRPmbuf m is still valid and should be 
 * sbappend()ed
 * 
 * NOTE: if you return 0 you MUST m_free() the SLIRPmbuf!
 */
int
tcp_emu(so, m)
	struct SLIRPsocket *so;
	struct SLIRPmbuf *m;
{
	u_int n1, n2, n3, n4, n5, n6;
	char buff[256];
	u_int32_t laddr;
	u_int lport;
	char *bptr;
	
	DEBUG_CALL("tcp_emu");
	DEBUG_ARG("so = %lx", (long)so);
	DEBUG_ARG("m = %lx", (long)m);
	
	switch(so->so_emu) {
		int x, i;
		
	 case EMU_IDENT:
		/*
		 * Identification protocol as per rfc-1413
		 */
		
		{
			struct SLIRPsocket *tmpso;
			struct sockaddr_in addr;
			socklen_t addrlen = sizeof(struct sockaddr_in);
			struct sbuf *so_rcv = &so->so_rcv;
			
			memcpy(so_rcv->sb_wptr, m->m_data, m->m_len);
			so_rcv->sb_wptr += m->m_len;
			so_rcv->sb_rptr += m->m_len;
			m->m_data[m->m_len] = 0; /* NULL terminate */
			if (strchr(m->m_data, '\r') || strchr(m->m_data, '\n')) {
				if (sscanf(so_rcv->sb_data, "%d%*[ ,]%d", &n1, &n2) == 2) {
					HTONS(n1);
					HTONS(n2);
					/* n2 is the one on our host */
					for (tmpso = tcb.so_next; tmpso != &tcb; tmpso = tmpso->so_next) {
						if (tmpso->so_laddr.s_addr == so->so_laddr.s_addr &&
						    tmpso->so_lport == n2 &&
						    tmpso->so_faddr.s_addr == so->so_faddr.s_addr &&
						    tmpso->so_fport == n1) {
							if (getsockname(tmpso->s,
								(struct sockaddr *)&addr, &addrlen) == 0)
							   n2 = ntohs(addr.sin_port);
							break;
						}
					}
				}
				so_rcv->sb_cc = sprintf(so_rcv->sb_data, "%d,%d\r\n", n1, n2);
				so_rcv->sb_rptr = so_rcv->sb_data;
				so_rcv->sb_wptr = so_rcv->sb_data + so_rcv->sb_cc;
			}
			m_free(m);
			return 0;
		}
		
#if 0
	 case EMU_RLOGIN:
		/*
		 * Rlogin emulation
		 * First we accumulate all the initial option negotiation,
		 * then fork_exec() rlogin according to the  options
		 */
		{
			int i, i2, n;
			char *ptr;
			char args[100];
			char term[100];
			struct sbuf *so_snd = &so->so_snd;
			struct sbuf *so_rcv = &so->so_rcv;
			
			/* First check if they have a priveladged port, or too much data has arrived */
			if (ntohs(so->so_lport) > 1023 || ntohs(so->so_lport) < 512 ||
			    (m->m_len + so_rcv->sb_wptr) > (so_rcv->sb_data + so_rcv->sb_datalen)) {
				memcpy(so_snd->sb_wptr, "Permission denied\n", 18);
				so_snd->sb_wptr += 18;
				so_snd->sb_cc += 18;
				tcp_sockclosed(sototcpcb(so));
				m_free(m);
				return 0;
			}
			
			/* Append the current data */
			memcpy(so_rcv->sb_wptr, m->m_data, m->m_len);
			so_rcv->sb_wptr += m->m_len;
			so_rcv->sb_rptr += m->m_len;
			m_free(m);
			
			/*
			 * Check if we have all the initial options,
			 * and build argument list to rlogin while we're here
			 */
			n = 0;
			ptr = so_rcv->sb_data;
			args[0] = 0;
			term[0] = 0;
			while (ptr < so_rcv->sb_wptr) {
				if (*ptr++ == 0) {
					n++;
					if (n == 2) {
						sprintf(args, "rlogin -l %s %s",
							ptr, inet_ntoa(so->so_faddr));
					} else if (n == 3) {
						i2 = so_rcv->sb_wptr - ptr;
						for (i = 0; i < i2; i++) {
							if (ptr[i] == '/') {
								ptr[i] = 0;
#ifdef HAVE_SETENV
								sprintf(term, "%s", ptr);
#else
								sprintf(term, "TERM=%s", ptr);
#endif
								ptr[i] = '/';
								break;
							}
						}
					}
				}
			}
			
			if (n != 4)
			   return 0;
			
			/* We have it, set our term variable and fork_exec() */
#ifdef HAVE_SETENV
			setenv("TERM", term, 1);
#else
			putenv(term);
#endif
			fork_exec(so, args, 2);
			term[0] = 0;
			so->so_emu = 0;
			
			/* And finally, send the client a 0 character */
			so_snd->sb_wptr[0] = 0;
			so_snd->sb_wptr++;
			so_snd->sb_cc++;
			
			return 0;
		}
		
	 case EMU_RSH:
		/*
		 * rsh emulation
		 * First we accumulate all the initial option negotiation,
		 * then rsh_exec() rsh according to the  options
		 */
		{
			int  n;
			char *ptr;
			char *user;
			char *args;
			struct sbuf *so_snd = &so->so_snd;
			struct sbuf *so_rcv = &so->so_rcv;
			
			/* First check if they have a priveladged port, or too much data has arrived */
			if (ntohs(so->so_lport) > 1023 || ntohs(so->so_lport) < 512 ||
			    (m->m_len + so_rcv->sb_wptr) > (so_rcv->sb_data + so_rcv->sb_datalen)) {
				memcpy(so_snd->sb_wptr, "Permission denied\n", 18);
				so_snd->sb_wptr += 18;
				so_snd->sb_cc += 18;
				tcp_sockclosed(sototcpcb(so));
				m_free(m);
				return 0;
			}
			
			/* Append the current data */
			memcpy(so_rcv->sb_wptr, m->m_data, m->m_len);
			so_rcv->sb_wptr += m->m_len;
			so_rcv->sb_rptr += m->m_len;
			m_free(m);
			
			/*
			 * Check if we have all the initial options,
			 * and build argument list to rlogin while we're here
			 */
			n = 0;
			ptr = so_rcv->sb_data;
			user="";
			args="";
			if (so->extra==NULL) {
				struct SLIRPsocket *ns;
				struct tcpcb* tp;
				int port=atoi(ptr);
				if (port <= 0) return 0;
                if (port > 1023 || port < 512) {
                  memcpy(so_snd->sb_wptr, "Permission denied\n", 18);
                  so_snd->sb_wptr += 18;
                  so_snd->sb_cc += 18;
                  tcp_sockclosed(sototcpcb(so));
                  return 0;
                }
				if ((ns=socreate()) == NULL)
                  return 0;
				if (tcp_attach(ns)<0) {
                  free(ns);
                  return 0;
				}

				ns->so_laddr=so->so_laddr;
				ns->so_lport=htons(port);

				(void) tcp_mss(sototcpcb(ns), 0);

				ns->so_faddr=so->so_faddr;
				ns->so_fport=htons(IPPORT_RESERVED-1); /* Use a fake port. */

				if (ns->so_faddr.s_addr == 0 || 
					ns->so_faddr.s_addr == loopback_addr.s_addr)
                  ns->so_faddr = alias_addr;

				ns->so_iptos = tcp_tos(ns);
				tp = sototcpcb(ns);
                
				tcp_template(tp);
                
				/* Compute window scaling to request.  */
				/*	while (tp->request_r_scale < TCP_MAX_WINSHIFT &&
				 *		(TCP_MAXWIN << tp->request_r_scale) < so->so_rcv.sb_hiwat)
				 *		tp->request_r_scale++;
				 */

                /*soisfconnecting(ns);*/

				tcpstat.tcps_connattempt++;
					
				tp->t_state = TCPS_SYN_SENT;
				tp->t_timer[TCPT_KEEP] = TCPTV_KEEP_INIT;
				tp->iss = tcp_iss; 
				tcp_iss += TCP_ISSINCR/2;
				tcp_sendseqinit(tp);
				tcp_output(tp);
				so->extra=ns;
			}
			while (ptr < so_rcv->sb_wptr) {
              if (*ptr++ == 0) {
                n++;
                if (n == 2) {
                  user=ptr;
                } else if (n == 3) {
                  args=ptr;
                }
              }
			}
			
			if (n != 4)
              return 0;
			
			rsh_exec(so,so->extra, user, inet_ntoa(so->so_faddr), args);
			so->so_emu = 0;
			so->extra=NULL;
			
			/* And finally, send the client a 0 character */
			so_snd->sb_wptr[0] = 0;
			so_snd->sb_wptr++;
			so_snd->sb_cc++;
			
			return 0;
		}

	 case EMU_CTL:
		{
			int num;
			struct sbuf *so_snd = &so->so_snd;
			struct sbuf *so_rcv = &so->so_rcv;
			
			/*
			 * If there is binary data here, we save it in so->so_m
			 */
			if (!so->so_m) {
			  int rxlen;
			  char *rxdata;
			  rxdata=mtod(m, char *);
			  for (rxlen=m->m_len; rxlen; rxlen--) {
			    if (*rxdata++ & 0x80) {
			      so->so_m = m;
			      return 0;
			    }
			  }
			} /* if(so->so_m==NULL) */
			
			/*
			 * Append the line
			 */
			sbappendsb(so_rcv, m);
			
			/* To avoid going over the edge of the buffer, we reset it */
			if (so_snd->sb_cc == 0)
			   so_snd->sb_wptr = so_snd->sb_rptr = so_snd->sb_data;
			
			/*
			 * A bit of a hack:
			 * If the first packet we get here is 1 byte long, then it
			 * was done in telnet character mode, therefore we must echo
			 * the characters as they come.  Otherwise, we echo nothing,
			 * because in linemode, the line is already echoed
			 * XXX two or more control connections won't work
			 */
			if (do_echo == -1) {
				if (m->m_len == 1) do_echo = 1;
				else do_echo = 0;
			}
			if (do_echo) {
			  sbappendsb(so_snd, m);
			  m_free(m);
			  tcp_output(sototcpcb(so)); /* XXX */
			} else
			  m_free(m);
			
			num = 0;
			while (num < so->so_rcv.sb_cc) {
				if (*(so->so_rcv.sb_rptr + num) == '\n' ||
				    *(so->so_rcv.sb_rptr + num) == '\r') {
					int n;
					
					*(so_rcv->sb_rptr + num) = 0;
					if (ctl_password && !ctl_password_ok) {
						/* Need a password */
						if (sscanf(so_rcv->sb_rptr, "pass %256s", buff) == 1) {
							if (strcmp(buff, ctl_password) == 0) {
								ctl_password_ok = 1;
								n = sprintf(so_snd->sb_wptr,
									    "Password OK.\r\n");
								goto do_prompt;
							}
						}
						n = sprintf(so_snd->sb_wptr,
					 "Error: Password required, log on with \"pass PASSWORD\"\r\n");
						goto do_prompt;
					}
					cfg_quitting = 0;
					n = do_config(so_rcv->sb_rptr, so, PRN_SPRINTF);
					if (!cfg_quitting) {
						/* Register the printed data */
do_prompt:
						so_snd->sb_cc += n;
						so_snd->sb_wptr += n;
						/* Add prompt */
						n = sprintf(so_snd->sb_wptr, "Slirp> ");
						so_snd->sb_cc += n;
						so_snd->sb_wptr += n;
					}
					/* Drop so_rcv data */
					so_rcv->sb_cc = 0;
					so_rcv->sb_wptr = so_rcv->sb_rptr = so_rcv->sb_data;
					tcp_output(sototcpcb(so)); /* Send the reply */
				}
				num++;
			}
			return 0;
		}
#endif		
        case EMU_FTP: /* ftp */
		*(m->m_data+m->m_len) = 0; /* NULL terminate for strstr */
		if ((bptr = (char *)strstr(m->m_data, "ORT")) != NULL) {
			/*
			 * Need to emulate the PORT command
			 */			
			x = sscanf(bptr, "ORT %d,%d,%d,%d,%d,%d\r\n%256[^\177]", 
				   &n1, &n2, &n3, &n4, &n5, &n6, buff);
			if (x < 6)
			   return 1;
			
			laddr = htonl((n1 << 24) | (n2 << 16) | (n3 << 8) | (n4));
			lport = htons((n5 << 8) | (n6));
			
			if ((so = solisten(0, laddr, lport, SS_FACCEPTONCE)) == NULL)
			   return 1;
			
			n6 = ntohs(so->so_fport);
			
			n5 = (n6 >> 8) & 0xff;
			n6 &= 0xff;
			
			laddr = ntohl(so->so_faddr.s_addr);
			
			n1 = ((laddr >> 24) & 0xff);
			n2 = ((laddr >> 16) & 0xff);
			n3 = ((laddr >> 8)  & 0xff);
			n4 =  (laddr & 0xff);
			
			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr,"ORT %d,%d,%d,%d,%d,%d\r\n%s", 
					    n1, n2, n3, n4, n5, n6, x==7?buff:"");
			return 1;
		} else if ((bptr = (char *)strstr(m->m_data, "27 Entering")) != NULL) {
			/*
			 * Need to emulate the PASV response
			 */
			x = sscanf(bptr, "27 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n%256[^\177]",
				   &n1, &n2, &n3, &n4, &n5, &n6, buff);
			if (x < 6)
			   return 1;
			
			laddr = htonl((n1 << 24) | (n2 << 16) | (n3 << 8) | (n4));
			lport = htons((n5 << 8) | (n6));
			
			if ((so = solisten(0, laddr, lport, SS_FACCEPTONCE)) == NULL)
			   return 1;
			
			n6 = ntohs(so->so_fport);
			
			n5 = (n6 >> 8) & 0xff;
			n6 &= 0xff;
			
			laddr = ntohl(so->so_faddr.s_addr);
			
			n1 = ((laddr >> 24) & 0xff);
			n2 = ((laddr >> 16) & 0xff);
			n3 = ((laddr >> 8)  & 0xff);
			n4 =  (laddr & 0xff);
			
			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr,"27 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n%s",
					    n1, n2, n3, n4, n5, n6, x==7?buff:"");
			
			return 1;
		}
		
		return 1;
				   
	 case EMU_KSH:
		/*
		 * The kshell (Kerberos rsh) and shell services both pass
		 * a local port port number to carry signals to the server
		 * and stderr to the client.  It is passed at the beginning
		 * of the connection as a NUL-terminated decimal ASCII string.
		 */
		so->so_emu = 0;
		for (lport = 0, i = 0; i < m->m_len-1; ++i) {
			if (m->m_data[i] < '0' || m->m_data[i] > '9')
				return 1;       /* invalid number */
			lport *= 10;
			lport += m->m_data[i] - '0';
		}
		if (m->m_data[m->m_len-1] == '\0' && lport != 0 &&
		    (so = solisten(0, so->so_laddr.s_addr, htons(lport), SS_FACCEPTONCE)) != NULL)
			m->m_len = sprintf(m->m_data, "%d", ntohs(so->so_fport))+1;
		return 1;
		
	 case EMU_IRC:
		/*
		 * Need to emulate DCC CHAT, DCC SEND and DCC MOVE
		 */
		*(m->m_data+m->m_len) = 0; /* NULL terminate the string for strstr */
		if ((bptr = (char *)strstr(m->m_data, "DCC")) == NULL)
			 return 1;
		
		/* The %256s is for the broken mIRC */
		if (sscanf(bptr, "DCC CHAT %256s %u %u", buff, &laddr, &lport) == 3) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;
			
			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC CHAT chat %lu %u%c\n",
			     (unsigned long)ntohl(so->so_faddr.s_addr),
			     ntohs(so->so_fport), 1);
		} else if (sscanf(bptr, "DCC SEND %256s %u %u %u", buff, &laddr, &lport, &n1) == 4) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;
			
			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC SEND %s %lu %u %u%c\n", 
			      buff, (unsigned long)ntohl(so->so_faddr.s_addr),
			      ntohs(so->so_fport), n1, 1);
		} else if (sscanf(bptr, "DCC MOVE %256s %u %u %u", buff, &laddr, &lport, &n1) == 4) {
			if ((so = solisten(0, htonl(laddr), htons(lport), SS_FACCEPTONCE)) == NULL)
				return 1;
			
			m->m_len = bptr - m->m_data; /* Adjust length */
			m->m_len += sprintf(bptr, "DCC MOVE %s %lu %u %u%c\n",
			      buff, (unsigned long)ntohl(so->so_faddr.s_addr),
			      ntohs(so->so_fport), n1, 1);
		}
		return 1;

	 case EMU_REALAUDIO:
                /* 
		 * RealAudio emulation - JP. We must try to parse the incoming
		 * data and try to find the two characters that contain the
		 * port number. Then we redirect an udp port and replace the
		 * number with the real port we got.
		 *
		 * The 1.0 beta versions of the player are not supported
		 * any more.
		 * 
		 * A typical packet for player version 1.0 (release version):
		 *        
		 * 0000:50 4E 41 00 05 
		 * 0000:00 01 00 02 1B D7 00 00 67 E6 6C DC 63 00 12 50 .....�..g�l�c..P
		 * 0010:4E 43 4C 49 45 4E 54 20 31 30 31 20 41 4C 50 48 NCLIENT 101 ALPH
		 * 0020:41 6C 00 00 52 00 17 72 61 66 69 6C 65 73 2F 76 Al..R..rafiles/v
		 * 0030:6F 61 2F 65 6E 67 6C 69 73 68 5F 2E 72 61 79 42 oa/english_.rayB
		 *         
		 * Now the port number 0x1BD7 is found at offset 0x04 of the
		 * Now the port number 0x1BD7 is found at offset 0x04 of the
		 * second packet. This time we received five bytes first and
		 * then the rest. You never know how many bytes you get.
		 *
		 * A typical packet for player version 2.0 (beta):
		 *        
		 * 0000:50 4E 41 00 06 00 02 00 00 00 01 00 02 1B C1 00 PNA...........�.
		 * 0010:00 67 75 78 F5 63 00 0A 57 69 6E 32 2E 30 2E 30 .gux�c..Win2.0.0
		 * 0020:2E 35 6C 00 00 52 00 1C 72 61 66 69 6C 65 73 2F .5l..R..rafiles/
		 * 0030:77 65 62 73 69 74 65 2F 32 30 72 65 6C 65 61 73 website/20releas
		 * 0040:65 2E 72 61 79 53 00 00 06 36 42                e.rayS...6B
		 *        
		 * Port number 0x1BC1 is found at offset 0x0d.
		 *      
		 * This is just a horrible switch statement. Variable ra tells
		 * us where we're going.
		 */
		
		bptr = m->m_data;
		while (bptr < m->m_data + m->m_len) {
			u_short p;
			static int ra = 0;
			char ra_tbl[4]; 
			
			ra_tbl[0] = 0x50;
			ra_tbl[1] = 0x4e;
			ra_tbl[2] = 0x41;
			ra_tbl[3] = 0;
			
			switch (ra) {
			 case 0:
			 case 2:
			 case 3:
				if (*bptr++ != ra_tbl[ra]) {
					ra = 0;
					continue;
				}
				break;
				
			 case 1:
				/*
				 * We may get 0x50 several times, ignore them
				 */
				if (*bptr == 0x50) {
					ra = 1;
					bptr++;
					continue;
				} else if (*bptr++ != ra_tbl[ra]) {
					ra = 0;
					continue;
				}
				break;
				
			 case 4: 
				/* 
				 * skip version number
				 */
				bptr++;
				break;
				
			 case 5: 
				/*
				 * The difference between versions 1.0 and
				 * 2.0 is here. For future versions of
				 * the player this may need to be modified.
				 */
				if (*(bptr + 1) == 0x02)
				   bptr += 8;
				else
				   bptr += 4;
				break;                          
				
			 case 6:
				/* This is the field containing the port
				 * number that RA-player is listening to.
				 */
				lport = (((u_char*)bptr)[0] << 8) 
				+ ((u_char *)bptr)[1];
				if (lport < 6970)      
				   lport += 256;   /* don't know why */
				if (lport < 6970 || lport > 7170)
				   return 1;       /* failed */
				
				/* try to get udp port between 6970 - 7170 */
				for (p = 6970; p < 7071; p++) {
					if (udp_listen( htons(p),
						       so->so_laddr.s_addr,
						       htons(lport),
						       SS_FACCEPTONCE)) {
						break;
					}
				}
				if (p == 7071)
				   p = 0;
				*(u_char *)bptr++ = (p >> 8) & 0xff;
				*(u_char *)bptr++ = p & 0xff;
				ra = 0; 
				return 1;   /* port redirected, we're done */
				break;  
				
			 default:
				ra = 0;                         
			}
			ra++;
		}
		return 1;                                
		
	 default:
		/* Ooops, not emulated, won't call tcp_emu again */
		so->so_emu = 0;
		return 1;
	}
}

/*
 * Do misc. config of SLiRP while its running.
 * Return 0 if this connections is to be closed, 1 otherwise,
 * return 2 if this is a command-line connection
 */
int
tcp_ctl(so)
	struct SLIRPsocket *so;
{
	struct sbuf *sb = &so->so_snd;
	int command;
 	struct ex_list *ex_ptr;
	int do_pty;
        //	struct SLIRPsocket *tmpso;
	
	DEBUG_CALL("tcp_ctl");
	DEBUG_ARG("so = %lx", (long )so);
	
#if 0
	/*
	 * Check if they're authorised
	 */
	if (ctl_addr.s_addr && (ctl_addr.s_addr == -1 || (so->so_laddr.s_addr != ctl_addr.s_addr))) {
		sb->sb_cc = sprintf(sb->sb_wptr,"Error: Permission denied.\r\n");
		sb->sb_wptr += sb->sb_cc;
		return 0;
	}
#endif	
	command = (ntohl(so->so_faddr.s_addr) & 0xff);
	
	switch(command) {
	default: /* Check for exec's */
		
		/*
		 * Check if it's pty_exec
		 */
		for (ex_ptr = exec_list; ex_ptr; ex_ptr = ex_ptr->ex_next) {
			if (ex_ptr->ex_fport == so->so_fport &&
			    command == ex_ptr->ex_addr) {
				do_pty = ex_ptr->ex_pty;
				goto do_exec;
			}
		}
		
		/*
		 * Nothing bound..
		 */
		/* tcp_fconnect(so); */
		
		/* FALLTHROUGH */
	case CTL_ALIAS:
	  sb->sb_cc = sprintf(sb->sb_wptr,
			      "Error: No application configured.\r\n");
	  sb->sb_wptr += sb->sb_cc;
	  return(0);

	do_exec:
		DEBUG_MISC((dfd, " executing %s \n",ex_ptr->ex_exec));
		return(fork_exec(so, ex_ptr->ex_exec, do_pty));
		
#if 0
	case CTL_CMD:
	   for (tmpso = tcb.so_next; tmpso != &tcb; tmpso = tmpso->so_next) {
	     if (tmpso->so_emu == EMU_CTL && 
		 !(tmpso->so_tcpcb? 
		   (tmpso->so_tcpcb->t_state & (TCPS_TIME_WAIT|TCPS_LAST_ACK))
		   :0)) {
	       /* Ooops, control connection already active */
	       sb->sb_cc = sprintf(sb->sb_wptr,"Sorry, already connected.\r\n");
	       sb->sb_wptr += sb->sb_cc;
	       return 0;
	     }
	   }
	   so->so_emu = EMU_CTL;
	   ctl_password_ok = 0;
	   sb->sb_cc = sprintf(sb->sb_wptr, "Slirp command-line ready (type \"help\" for help).\r\nSlirp> ");
	   sb->sb_wptr += sb->sb_cc;
	   do_echo=-1;
	   return(2);
#endif
	}
}
