/*
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)ip_input.c	8.2 (Berkeley) 1/4/94
 * ip_input.c,v 1.11 1994/11/16 10:17:08 jkh Exp
 */

/*
 * Changes and additions relating to SLiRP are
 * Copyright (c) 1995 Danny Gasparovski.
 * 
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */
#include <stdint.h>

#include "slirp.h"
#include "ip_icmp.h"

int ip_defttl;
struct ipstat ipstat;
struct ipq ipq;

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void
ip_init()
{
	ipq.next = ipq.prev = (ipqp_32)&ipq;
	ip_id = tt.tv_sec & 0xffff;
	udp_init();
	tcp_init();
	ip_defttl = IPDEFTTL;
}

/*
 * Ip input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void
ip_input(m)
	struct SLIRPmbuf *m;
{
	register struct ip *ip;
	u_int hlen;
	
	DEBUG_CALL("ip_input");
	DEBUG_ARG("m = %lx", (long)m);
	DEBUG_ARG("m_len = %d", m->m_len);

	ipstat.ips_total++;
	
	if (m->m_len < sizeof (struct ip)) {
		ipstat.ips_toosmall++;
		return;
	}
	
	ip = mtod(m, struct ip *);
	
	if (ip->ip_v != IPVERSION) {
		ipstat.ips_badvers++;
		goto bad;
	}

	hlen = ip->ip_hl << 2;
	if (hlen<sizeof(struct ip ) || hlen>m->m_len) {/* min header length */
	  ipstat.ips_badhlen++;                     /* or packet too short */
	  goto bad;
	}

        /* keep ip header intact for ICMP reply
	 * ip->ip_sum = cksum(m, hlen); 
	 * if (ip->ip_sum) { 
	 */
	if(cksum(m,hlen)) {
	  ipstat.ips_badsum++;
	  goto bad;
	}

	/*
	 * Convert fields to host representation.
	 */
	NTOHS(ip->ip_len);
	if (ip->ip_len < hlen) {
		ipstat.ips_badlen++;
		goto bad;
	}
	NTOHS(ip->ip_id);
	NTOHS(ip->ip_off);

	/*
	 * Check that the amount of data in the buffers
	 * is as at least much as the IP header would have us expect.
	 * Trim SLIRPmbufs if longer than we expect.
	 * Drop packet if shorter than we expect.
	 */
	if (m->m_len < ip->ip_len) {
		ipstat.ips_tooshort++;
		goto bad;
	}
	/* Should drop packet if SLIRPmbuf too long? hmmm... */
	if (m->m_len > ip->ip_len)
	   m_adj(m, ip->ip_len - m->m_len);

	/* check ip_ttl for a correct ICMP reply */
	if(ip->ip_ttl==0 || ip->ip_ttl==1) {
	  icmp_error(m, ICMP_TIMXCEED,ICMP_TIMXCEED_INTRANS, 0,"ttl");
	  goto bad;
	}

	/*
	 * Process options and, if not destined for us,
	 * ship it on.  ip_dooptions returns 1 when an
	 * error was detected (causing an icmp message
	 * to be sent and the original packet to be freed).
	 */
/* We do no IP options */
/*	if (hlen > sizeof (struct ip) && ip_dooptions(m))
 *		goto next;
 */
	/*
	 * If offset or IP_MF are set, must reassemble.
	 * Otherwise, nothing need be done.
	 * (We could look in the reassembly queue to see
	 * if the packet was previously fragmented,
	 * but it's not worth the time; just let them time out.)
	 * 
	 * XXX This should fail, don't fragment yet
	 */
	if (ip->ip_off &~ IP_DF) {
	  register struct ipq *fp;
		/*
		 * Look for queue of fragments
		 * of this datagram.
		 */
		for (fp = (struct ipq *) ipq.next; fp != &ipq;
		     fp = (struct ipq *) fp->next)
		  if (ip->ip_id == fp->ipq_id &&
		      ip->ip_src.s_addr == fp->ipq_src.s_addr &&
		      ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
		      ip->ip_p == fp->ipq_p)
		    goto found;
		fp = 0;
	found:

		/*
		 * Adjust ip_len to not reflect header,
		 * set ip_mff if more fragments are expected,
		 * convert offset of this to bytes.
		 */
		ip->ip_len -= hlen;
		if (ip->ip_off & IP_MF)
		  ((struct ipasfrag *)ip)->ipf_mff |= 1;
		else 
		  ((struct ipasfrag *)ip)->ipf_mff &= ~1;

		ip->ip_off <<= 3;

		/*
		 * If datagram marked as having more fragments
		 * or if this is not the first fragment,
		 * attempt reassembly; if it succeeds, proceed.
		 */
		if (((struct ipasfrag *)ip)->ipf_mff & 1 || ip->ip_off) {
			ipstat.ips_fragments++;
			ip = ip_reass((struct ipasfrag *)ip, fp);
			if (ip == 0)
				return;
			ipstat.ips_reassembled++;
			m = dtom(ip);
		} else
			if (fp)
		   	   ip_freef(fp);

	} else
		ip->ip_len -= hlen;

	/*
	 * Switch out to protocol's input routine.
	 */
	ipstat.ips_delivered++;
	switch (ip->ip_p) {
	 case IPPROTO_TCP:
		tcp_input(m, hlen, (struct SLIRPsocket *)NULL);
		break;
	 case IPPROTO_UDP:
		udp_input(m, hlen);
		break;
	 case IPPROTO_ICMP:
		icmp_input(m, hlen);
		break;
	 default:
		ipstat.ips_noproto++;
		m_free(m);
	}
	return;
bad:
	m_freem(m);
	return;
}

/*
 * Take incoming datagram fragment and try to
 * reassemble it into whole datagram.  If a chain for
 * reassembly of this datagram already exists, then it
 * is given as fp; otherwise have to make a chain.
 */
struct ip *
ip_reass(ip, fp)
	register struct ipasfrag *ip;
	register struct ipq *fp;
{
	register struct SLIRPmbuf *m = dtom(ip);
	register struct ipasfrag *q;
	int hlen = ip->ip_hl << 2;
	int i, next;
	
	DEBUG_CALL("ip_reass");
	DEBUG_ARG("ip = %lx", (long)ip);
	DEBUG_ARG("fp = %lx", (long)fp);
	DEBUG_ARG("m = %lx", (long)m);

	/*
	 * Presence of header sizes in SLIRPmbufs
	 * would confuse code below.
         * Fragment m_data is concatenated.
	 */
	m->m_data += hlen;
	m->m_len -= hlen;

	/*
	 * If first fragment to arrive, create a reassembly queue.
	 */
	if (fp == 0) {
	  struct SLIRPmbuf *t;
	  if ((t = m_get()) == NULL) goto dropfrag;
	  fp = mtod(t, struct ipq *);
	  insque_32(fp, &ipq);
	  fp->ipq_ttl = IPFRAGTTL;
	  fp->ipq_p = ip->ip_p;
	  fp->ipq_id = ip->ip_id;
	  fp->ipq_next = fp->ipq_prev = (ipasfragp_32)fp;
	  fp->ipq_src = ((struct ip *)ip)->ip_src;
	  fp->ipq_dst = ((struct ip *)ip)->ip_dst;
	  q = (struct ipasfrag *)fp;
	  goto insert;
	}
	
	/*
	 * Find a segment which begins after this one does.
	 */
	for (q = (struct ipasfrag *)fp->ipq_next; q != (struct ipasfrag *)fp;
	    q = (struct ipasfrag *)q->ipf_next)
		if (q->ip_off > ip->ip_off)
			break;

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (q->ipf_prev != (ipasfragp_32)fp) {
		i = ((struct ipasfrag *)(q->ipf_prev))->ip_off +
		  ((struct ipasfrag *)(q->ipf_prev))->ip_len - ip->ip_off;
		if (i > 0) {
			if (i >= ip->ip_len)
				goto dropfrag;
			m_adj(dtom(ip), i);
			ip->ip_off += i;
			ip->ip_len -= i;
		}
	}

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	while (q != (struct ipasfrag *)fp && ip->ip_off + ip->ip_len > q->ip_off) {
		i = (ip->ip_off + ip->ip_len) - q->ip_off;
		if (i < q->ip_len) {
			q->ip_len -= i;
			q->ip_off += i;
			m_adj(dtom(q), i);
			break;
		}
		q = (struct ipasfrag *) q->ipf_next;
		m_freem(dtom((struct ipasfrag *) q->ipf_prev));
		ip_deq((struct ipasfrag *) q->ipf_prev);
	}

insert:
	/*
	 * Stick new segment in its place;
	 * check for complete reassembly.
	 */
	ip_enq(ip, (struct ipasfrag *) q->ipf_prev);
	next = 0;
	for (q = (struct ipasfrag *) fp->ipq_next; q != (struct ipasfrag *)fp;
	     q = (struct ipasfrag *) q->ipf_next) {
		if (q->ip_off != next)
			return (0);
		next += q->ip_len;
	}
	if (((struct ipasfrag *)(q->ipf_prev))->ipf_mff & 1)
		return (0);

	/*
	 * Reassembly is complete; concatenate fragments.
	 */
	q = (struct ipasfrag *) fp->ipq_next;
	m = dtom(q);

	q = (struct ipasfrag *) q->ipf_next;
	while (q != (struct ipasfrag *)fp) {
	  struct SLIRPmbuf *t;
	  t = dtom(q);
	  q = (struct ipasfrag *) q->ipf_next;
	  m_cat(m, t);
	}

	/*
	 * Create header for new ip packet by
	 * modifying header of first packet;
	 * dequeue and discard fragment reassembly header.
	 * Make header visible.
	 */
	ip = (struct ipasfrag *) fp->ipq_next;

	/*
	 * If the fragments concatenated to an SLIRPmbuf that's
	 * bigger than the total size of the fragment, then and
	 * m_ext buffer was alloced. But fp->ipq_next points to
	 * the old buffer (in the SLIRPmbuf), so we must point ip
	 * into the new buffer.
	 */
	if (m->m_flags & M_EXT) {
	  int delta;
	  delta = (char *)ip - m->m_dat;
	  ip = (struct ipasfrag *)(m->m_ext + delta);
	}

	/* DEBUG_ARG("ip = %lx", (long)ip); 
	 * ip=(struct ipasfrag *)m->m_data; */

	ip->ip_len = next;
	ip->ipf_mff &= ~1;
	((struct ip *)ip)->ip_src = fp->ipq_src;
	((struct ip *)ip)->ip_dst = fp->ipq_dst;
	remque_32(fp);
	(void) m_free(dtom(fp));
	m = dtom(ip);
	m->m_len += (ip->ip_hl << 2);
	m->m_data -= (ip->ip_hl << 2);

	return ((struct ip *)ip);

dropfrag:
	ipstat.ips_fragdropped++;
	m_freem(m);
	return (0);
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 */
void
ip_freef(fp)
	struct ipq *fp;
{
	register struct ipasfrag *q, *p;

	for (q = (struct ipasfrag *) fp->ipq_next; q != (struct ipasfrag *)fp;
	    q = p) {
		p = (struct ipasfrag *) q->ipf_next;
		ip_deq(q);
		m_freem(dtom(q));
	}
	remque_32(fp);
	(void) m_free(dtom(fp));
}

/*
 * Put an ip fragment on a reassembly chain.
 * Like insque, but pointers in middle of structure.
 */
void
ip_enq(p, prev)
	register struct ipasfrag *p, *prev;
{
	DEBUG_CALL("ip_enq");
	DEBUG_ARG("prev = %lx", (long)prev);
	p->ipf_prev = (ipasfragp_32) prev;
	p->ipf_next = prev->ipf_next;
	((struct ipasfrag *)(prev->ipf_next))->ipf_prev = (ipasfragp_32) p;
	prev->ipf_next = (ipasfragp_32) p;
}

/*
 * To ip_enq as remque is to insque.
 */
void
ip_deq(p)
	register struct ipasfrag *p;
{
	((struct ipasfrag *)(p->ipf_prev))->ipf_next = p->ipf_next;
	((struct ipasfrag *)(p->ipf_next))->ipf_prev = p->ipf_prev;
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void
ip_slowtimo()
{
	register struct ipq *fp;
	
	DEBUG_CALL("ip_slowtimo");
	
	fp = (struct ipq *) ipq.next;
	if (fp == 0)
	   return;

	while (fp != &ipq) {
		--fp->ipq_ttl;
		fp = (struct ipq *) fp->next;
		if (((struct ipq *)(fp->prev))->ipq_ttl == 0) {
			ipstat.ips_fragtimeout++;
			ip_freef((struct ipq *) fp->prev);
		}
	}
}

/*
 * Do option processing on a datagram,
 * possibly discarding it if bad options are encountered,
 * or forwarding it if source-routed.
 * Returns 1 if packet has been forwarded/freed,
 * 0 if the packet should be processed further.
 */

#ifdef notdef

int
ip_dooptions(m)
	struct SLIRPmbuf *m;
{
	register struct ip *ip = mtod(m, struct ip *);
	register u_char *cp;
	register struct ip_timestamp *ipt;
	register struct in_ifaddr *ia;
/*	int opt, optlen, cnt, off, code, type = ICMP_PARAMPROB, forward = 0; */
	int opt, optlen, cnt, off, code, type, forward = 0;
	struct in_addr *sin, dst;
typedef u_int32_t n_time;
	n_time ntime;

	dst = ip->ip_dst;
	cp = (u_char *)(ip + 1);
	cnt = (ip->ip_hl << 2) - sizeof (struct ip);
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[IPOPT_OPTVAL];
		if (opt == IPOPT_EOL)
			break;
		if (opt == IPOPT_NOP)
			optlen = 1;
		else {
			optlen = cp[IPOPT_OLEN];
			if (optlen <= 0 || optlen > cnt) {
				code = &cp[IPOPT_OLEN] - (u_char *)ip;
				goto bad;
			}
		}
		switch (opt) {

		default:
			break;

		/*
		 * Source routing with record.
		 * Find interface with current destination address.
		 * If none on this machine then drop if strictly routed,
		 * or do nothing if loosely routed.
		 * Record interface address and bring up next address
		 * component.  If strictly routed make sure next
		 * address is on directly accessible net.
		 */
		case IPOPT_LSRR:
		case IPOPT_SSRR:
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			ipaddr.sin_addr = ip->ip_dst;
			ia = (struct in_ifaddr *)
				ifa_ifwithaddr((struct sockaddr *)&ipaddr);
			if (ia == 0) {
				if (opt == IPOPT_SSRR) {
					type = ICMP_UNREACH;
					code = ICMP_UNREACH_SRCFAIL;
					goto bad;
				}
				/*
				 * Loose routing, and not at next destination
				 * yet; nothing to do except forward.
				 */
				break;
			}
			off--;			/ * 0 origin *  /
			if (off > optlen - sizeof(struct in_addr)) {
				/*
				 * End of source route.  Should be for us.
				 */
				save_rte(cp, ip->ip_src);
				break;
			}
			/*
			 * locate outgoing interface
			 */
			bcopy((SLIRPcaddr_t)(cp + off), (SLIRPcaddr_t)&ipaddr.sin_addr,
			    sizeof(ipaddr.sin_addr));
			if (opt == IPOPT_SSRR) {
#define	INA	struct in_ifaddr *
#define	SA	struct sockaddr *
 			    if ((ia = (INA)ifa_ifwithdstaddr((SA)&ipaddr)) == 0)
				ia = (INA)ifa_ifwithnet((SA)&ipaddr);
			} else
				ia = ip_rtaddr(ipaddr.sin_addr);
			if (ia == 0) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_SRCFAIL;
				goto bad;
			}
			ip->ip_dst = ipaddr.sin_addr;
			bcopy((SLIRPcaddr_t)&(IA_SIN(ia)->sin_addr),
			    (SLIRPcaddr_t)(cp + off), sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			/*
			 * Let ip_intr's mcast routing check handle mcast pkts
			 */
			forward = !IN_MULTICAST(ntohl(ip->ip_dst.s_addr));
			break;

		case IPOPT_RR:
			if ((off = cp[IPOPT_OFFSET]) < IPOPT_MINOFF) {
				code = &cp[IPOPT_OFFSET] - (u_char *)ip;
				goto bad;
			}
			/*
			 * If no space remains, ignore.
			 */
			off--;			 * 0 origin *
			if (off > optlen - sizeof(struct in_addr))
				break;
			bcopy((SLIRPcaddr_t)(&ip->ip_dst), (SLIRPcaddr_t)&ipaddr.sin_addr,
			    sizeof(ipaddr.sin_addr));
			/*
			 * locate outgoing interface; if we're the destination,
			 * use the incoming interface (should be same).
			 */
			if ((ia = (INA)ifa_ifwithaddr((SA)&ipaddr)) == 0 &&
			    (ia = ip_rtaddr(ipaddr.sin_addr)) == 0) {
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_HOST;
				goto bad;
			}
			bcopy((SLIRPcaddr_t)&(IA_SIN(ia)->sin_addr),
			    (SLIRPcaddr_t)(cp + off), sizeof(struct in_addr));
			cp[IPOPT_OFFSET] += sizeof(struct in_addr);
			break;

		case IPOPT_TS:
			code = cp - (u_char *)ip;
			ipt = (struct ip_timestamp *)cp;
			if (ipt->ipt_len < 5)
				goto bad;
			if (ipt->ipt_ptr > ipt->ipt_len - sizeof (int32_t)) {
				if (++ipt->ipt_oflw == 0)
					goto bad;
				break;
			}
			sin = (struct in_addr *)(cp + ipt->ipt_ptr - 1);
			switch (ipt->ipt_flg) {

			case IPOPT_TS_TSONLY:
				break;

			case IPOPT_TS_TSANDADDR:
				if (ipt->ipt_ptr + sizeof(n_time) +
				    sizeof(struct in_addr) > ipt->ipt_len)
					goto bad;
				ipaddr.sin_addr = dst;
				ia = (INA)ifaof_ i f p foraddr((SA)&ipaddr,
							    m->m_pkthdr.rcvif);
				if (ia == 0)
					continue;
				bcopy((SLIRPcaddr_t)&IA_SIN(ia)->sin_addr,
				    (SLIRPcaddr_t)sin, sizeof(struct in_addr));
				ipt->ipt_ptr += sizeof(struct in_addr);
				break;

			case IPOPT_TS_PRESPEC:
				if (ipt->ipt_ptr + sizeof(n_time) +
				    sizeof(struct in_addr) > ipt->ipt_len)
					goto bad;
				bcopy((SLIRPcaddr_t)sin, (SLIRPcaddr_t)&ipaddr.sin_addr,
				    sizeof(struct in_addr));
				if (ifa_ifwithaddr((SA)&ipaddr) == 0)
					continue;
				ipt->ipt_ptr += sizeof(struct in_addr);
				break;

			default:
				goto bad;
			}
			ntime = iptime();
			bcopy((SLIRPcaddr_t)&ntime, (SLIRPcaddr_t)cp + ipt->ipt_ptr - 1,
			    sizeof(n_time));
			ipt->ipt_ptr += sizeof(n_time);
		}
	}
	if (forward) {
		ip_forward(m, 1);
		return (1);
	}
		}
	}
	return (0);
bad:
	/* ip->ip_len -= ip->ip_hl << 2;   XXX icmp_error adds in hdr length */

/* Not yet */
 	icmp_error(m, type, code, 0, 0);

	ipstat.ips_badoptions++;
	return (1);
}

#endif /* notdef */

/*
 * Strip out IP options, at higher
 * level protocol in the kernel.
 * Second argument is buffer to which options
 * will be moved, and return value is their length.
 * (XXX) should be deleted; last arg currently ignored.
 */
void
ip_stripoptions(m, mopt)
	struct SLIRPmbuf *m;
	struct SLIRPmbuf *mopt;
{
	register int i;
	struct ip *ip = mtod(m, struct ip *);
	register SLIRPcaddr_t opts;
	int olen;

	olen = (ip->ip_hl<<2) - sizeof (struct ip);
	opts = (SLIRPcaddr_t)(ip + 1);
	i = m->m_len - (sizeof (struct ip) + olen);
	memcpy(opts, opts  + olen, (unsigned)i);
	m->m_len -= olen;
	
	ip->ip_hl = sizeof(struct ip) >> 2;
}
