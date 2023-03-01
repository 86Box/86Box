/* SPDX-License-Identifier: BSD-3-Clause */
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
 */

#include "slirp.h"
#include "ip_icmp.h"
#include <stddef.h>

static struct ip *ip_reass(Slirp *slirp, struct ip *ip, struct ipq *fp);
static void ip_freef(Slirp *slirp, struct ipq *fp);
static void ip_enq(register struct ipasfrag *p, register struct ipasfrag *prev);
static void ip_deq(register struct ipasfrag *p);

/*
 * IP initialization: fill in IP protocol switch table.
 * All protocols not implemented in kernel go to raw IP protocol handler.
 */
void ip_init(Slirp *slirp)
{
    slirp->ipq.ip_link.next = slirp->ipq.ip_link.prev = &slirp->ipq.ip_link;
    udp_init(slirp);
    tcp_init(slirp);
    icmp_init(slirp);
}

void ip_cleanup(Slirp *slirp)
{
    udp_cleanup(slirp);
    tcp_cleanup(slirp);
    icmp_cleanup(slirp);
}

/*
 * Ip input routine.  Checksum and byte swap header.  If fragmented
 * try to reassemble.  Process options.  Pass to next level.
 */
void ip_input(struct mbuf *m)
{
    Slirp *slirp = m->slirp;
    M_DUP_DEBUG(slirp, m, 0, TCPIPHDR_DELTA);

    register struct ip *ip;
    int hlen;

    if (!slirp->in_enabled) {
        goto bad;
    }

    DEBUG_CALL("ip_input");
    DEBUG_ARG("m = %p", m);
    DEBUG_ARG("m_len = %d", m->m_len);

    if (m->m_len < sizeof(struct ip)) {
        goto bad;
    }

    ip = mtod(m, struct ip *);

    if (ip->ip_v != IPVERSION) {
        goto bad;
    }

    hlen = ip->ip_hl << 2;
    if (hlen < sizeof(struct ip) || hlen > m->m_len) { /* min header length */
        goto bad; /* or packet too short */
    }

    /* keep ip header intact for ICMP reply
     * ip->ip_sum = cksum(m, hlen);
     * if (ip->ip_sum) {
     */
    if (cksum(m, hlen)) {
        goto bad;
    }

    /*
     * Convert fields to host representation.
     */
    NTOHS(ip->ip_len);
    if (ip->ip_len < hlen) {
        goto bad;
    }
    NTOHS(ip->ip_id);
    NTOHS(ip->ip_off);

    /*
     * Check that the amount of data in the buffers
     * is as at least much as the IP header would have us expect.
     * Trim mbufs if longer than we expect.
     * Drop packet if shorter than we expect.
     */
    if (m->m_len < ip->ip_len) {
        goto bad;
    }

    /* Should drop packet if mbuf too long? hmmm... */
    if (m->m_len > ip->ip_len)
        m_adj(m, ip->ip_len - m->m_len);

    /* check ip_ttl for a correct ICMP reply */
    if (ip->ip_ttl == 0) {
        icmp_send_error(m, ICMP_TIMXCEED, ICMP_TIMXCEED_INTRANS, 0, "ttl");
        goto bad;
    }

    /*
     * If offset or IP_MF are set, must reassemble.
     * Otherwise, nothing need be done.
     * (We could look in the reassembly queue to see
     * if the packet was previously fragmented,
     * but it's not worth the time; just let them time out.)
     *
     * XXX This should fail, don't fragment yet
     */
    if (ip->ip_off & ~IP_DF) {
        register struct ipq *fp;
        struct qlink *l;
        /*
         * Look for queue of fragments
         * of this datagram.
         */
        for (l = slirp->ipq.ip_link.next; l != &slirp->ipq.ip_link;
             l = l->next) {
            fp = container_of(l, struct ipq, ip_link);
            if (ip->ip_id == fp->ipq_id &&
                ip->ip_src.s_addr == fp->ipq_src.s_addr &&
                ip->ip_dst.s_addr == fp->ipq_dst.s_addr &&
                ip->ip_p == fp->ipq_p)
                goto found;
        }
        fp = NULL;
    found:

        /*
         * Adjust ip_len to not reflect header,
         * set ip_mff if more fragments are expected,
         * convert offset of this to bytes.
         */
        ip->ip_len -= hlen;
        if (ip->ip_off & IP_MF)
            ip->ip_tos |= 1;
        else
            ip->ip_tos &= ~1;

        ip->ip_off <<= 3;

        /*
         * If datagram marked as having more fragments
         * or if this is not the first fragment,
         * attempt reassembly; if it succeeds, proceed.
         */
        if (ip->ip_tos & 1 || ip->ip_off) {
            ip = ip_reass(slirp, ip, fp);
            if (ip == NULL)
                return;
            m = dtom(slirp, ip);
        } else if (fp)
            ip_freef(slirp, fp);

    } else
        ip->ip_len -= hlen;

    /*
     * Switch out to protocol's input routine.
     */
    switch (ip->ip_p) {
    case IPPROTO_TCP:
        tcp_input(m, hlen, (struct socket *)NULL, AF_INET);
        break;
    case IPPROTO_UDP:
        udp_input(m, hlen);
        break;
    case IPPROTO_ICMP:
        icmp_input(m, hlen);
        break;
    default:
        m_free(m);
    }
    return;
bad:
    m_free(m);
}

#define iptofrag(P) ((struct ipasfrag *)(((char *)(P)) - sizeof(struct qlink)))
#define fragtoip(P) ((struct ip *)(((char *)(P)) + sizeof(struct qlink)))
/*
 * Take incoming datagram fragment and try to
 * reassemble it into whole datagram.  If a chain for
 * reassembly of this datagram already exists, then it
 * is given as fp; otherwise have to make a chain.
 */
static struct ip *ip_reass(Slirp *slirp, struct ip *ip, struct ipq *fp)
{
    register struct mbuf *m = dtom(slirp, ip);
    register struct ipasfrag *q;
    int hlen = ip->ip_hl << 2;
    int i, next;

    DEBUG_CALL("ip_reass");
    DEBUG_ARG("ip = %p", ip);
    DEBUG_ARG("fp = %p", fp);
    DEBUG_ARG("m = %p", m);

    /*
     * Presence of header sizes in mbufs
     * would confuse code below.
     * Fragment m_data is concatenated.
     */
    m->m_data += hlen;
    m->m_len -= hlen;

    /*
     * If first fragment to arrive, create a reassembly queue.
     */
    if (fp == NULL) {
        struct mbuf *t = m_get(slirp);

        if (t == NULL) {
            goto dropfrag;
        }
        fp = mtod(t, struct ipq *);
        slirp_insque(&fp->ip_link, &slirp->ipq.ip_link);
        fp->ipq_ttl = IPFRAGTTL;
        fp->ipq_p = ip->ip_p;
        fp->ipq_id = ip->ip_id;
        fp->frag_link.next = fp->frag_link.prev = &fp->frag_link;
        fp->ipq_src = ip->ip_src;
        fp->ipq_dst = ip->ip_dst;
        q = (struct ipasfrag *)fp;
        goto insert;
    }

    /*
     * Find a segment which begins after this one does.
     */
    for (q = fp->frag_link.next; q != (struct ipasfrag *)&fp->frag_link;
         q = q->ipf_next)
        if (q->ipf_off > ip->ip_off)
            break;

    /*
     * If there is a preceding segment, it may provide some of
     * our data already.  If so, drop the data from the incoming
     * segment.  If it provides all of our data, drop us.
     */
    if (q->ipf_prev != &fp->frag_link) {
        struct ipasfrag *pq = q->ipf_prev;
        i = pq->ipf_off + pq->ipf_len - ip->ip_off;
        if (i > 0) {
            if (i >= ip->ip_len)
                goto dropfrag;
            m_adj(dtom(slirp, ip), i);
            ip->ip_off += i;
            ip->ip_len -= i;
        }
    }

    /*
     * While we overlap succeeding segments trim them or,
     * if they are completely covered, dequeue them.
     */
    while (q != (struct ipasfrag *)&fp->frag_link &&
           ip->ip_off + ip->ip_len > q->ipf_off) {
        struct ipasfrag *prev;
        i = (ip->ip_off + ip->ip_len) - q->ipf_off;
        if (i < q->ipf_len) {
            q->ipf_len -= i;
            q->ipf_off += i;
            m_adj(dtom(slirp, q), i);
            break;
        }
        prev = q;
        q = q->ipf_next;
        ip_deq(prev);
        m_free(dtom(slirp, prev));
    }

insert:
    /*
     * Stick new segment in its place;
     * check for complete reassembly.
     */
    ip_enq(iptofrag(ip), q->ipf_prev);
    next = 0;
    for (q = fp->frag_link.next; q != (struct ipasfrag *)&fp->frag_link;
         q = q->ipf_next) {
        if (q->ipf_off != next)
            return NULL;
        next += q->ipf_len;
    }
    if (((struct ipasfrag *)(q->ipf_prev))->ipf_tos & 1)
        return NULL;

    /*
     * Reassembly is complete; concatenate fragments.
     */
    q = fp->frag_link.next;
    m = dtom(slirp, q);
    int delta = (char *)q - (m->m_flags & M_EXT ? m->m_ext : m->m_dat);

    q = (struct ipasfrag *)q->ipf_next;
    while (q != (struct ipasfrag *)&fp->frag_link) {
        struct mbuf *t = dtom(slirp, q);
        q = (struct ipasfrag *)q->ipf_next;
        m_cat(m, t);
    }

    /*
     * Create header for new ip packet by
     * modifying header of first packet;
     * dequeue and discard fragment reassembly header.
     * Make header visible.
     */
    q = fp->frag_link.next;

    /*
     * If the fragments concatenated to an mbuf that's bigger than the total
     * size of the fragment and the mbuf was not already using an m_ext buffer,
     * then an m_ext buffer was allocated. But fp->ipq_next points to the old
     * buffer (in the mbuf), so we must point ip into the new buffer.
     */
    if (m->m_flags & M_EXT) {
        q = (struct ipasfrag *)(m->m_ext + delta);
    }

    ip = fragtoip(q);
    ip->ip_len = next;
    ip->ip_tos &= ~1;
    ip->ip_src = fp->ipq_src;
    ip->ip_dst = fp->ipq_dst;
    slirp_remque(&fp->ip_link);
    m_free(dtom(slirp, fp));
    m->m_len += (ip->ip_hl << 2);
    m->m_data -= (ip->ip_hl << 2);

    return ip;

dropfrag:
    m_free(m);
    return NULL;
}

/*
 * Free a fragment reassembly header and all
 * associated datagrams.
 */
static void ip_freef(Slirp *slirp, struct ipq *fp)
{
    register struct ipasfrag *q, *p;

    for (q = fp->frag_link.next; q != (struct ipasfrag *)&fp->frag_link;
         q = p) {
        p = q->ipf_next;
        ip_deq(q);
        m_free(dtom(slirp, q));
    }
    slirp_remque(&fp->ip_link);
    m_free(dtom(slirp, fp));
}

/*
 * Put an ip fragment on a reassembly chain.
 * Like slirp_insque, but pointers in middle of structure.
 */
static void ip_enq(register struct ipasfrag *p, register struct ipasfrag *prev)
{
    DEBUG_CALL("ip_enq");
    DEBUG_ARG("prev = %p", prev);
    p->ipf_prev = prev;
    p->ipf_next = prev->ipf_next;
    ((struct ipasfrag *)(prev->ipf_next))->ipf_prev = p;
    prev->ipf_next = p;
}

/*
 * To ip_enq as slirp_remque is to slirp_insque.
 */
static void ip_deq(register struct ipasfrag *p)
{
    ((struct ipasfrag *)(p->ipf_prev))->ipf_next = p->ipf_next;
    ((struct ipasfrag *)(p->ipf_next))->ipf_prev = p->ipf_prev;
}

/*
 * IP timer processing;
 * if a timer expires on a reassembly
 * queue, discard it.
 */
void ip_slowtimo(Slirp *slirp)
{
    struct qlink *l;

    DEBUG_CALL("ip_slowtimo");

    l = slirp->ipq.ip_link.next;

    if (l == NULL)
        return;

    while (l != &slirp->ipq.ip_link) {
        struct ipq *fp = container_of(l, struct ipq, ip_link);
        l = l->next;
        if (--fp->ipq_ttl == 0) {
            ip_freef(slirp, fp);
        }
    }
}

/*
 * Strip out IP options, at higher
 * level protocol in the kernel.
 * Second argument is buffer to which options
 * will be moved, and return value is their length.
 * (XXX) should be deleted; last arg currently ignored.
 */
void ip_stripoptions(register struct mbuf *m, struct mbuf *mopt)
{
    register int i;
    struct ip *ip = mtod(m, struct ip *);
    register char *opts;
    int olen;

    olen = (ip->ip_hl << 2) - sizeof(struct ip);
    opts = (char *)(ip + 1);
    i = m->m_len - (sizeof(struct ip) + olen);
    memmove(opts, opts + olen, (unsigned)i);
    m->m_len -= olen;

    ip->ip_hl = sizeof(struct ip) >> 2;
}
