/*
 * tftp.c - a simple, read-only tftp server for qemu
 * 
 * Copyright (c) 2004 Magnus Damm <damm@opensource.se>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "slirp.h"
#include "../ibm.h"

struct tftp_session {
    int in_use;
    char filename[TFTP_FILENAME_MAX];
    
    struct in_addr client_ip;
    u_int16_t client_port;
    
    int timestamp;
};

struct tftp_session tftp_sessions[TFTP_SESSIONS_MAX];

const char *tftp_prefix;

static void tftp_session_update(struct tftp_session *spt)
{
    spt->timestamp = curtime;
    spt->in_use = 1;
}

static void tftp_session_terminate(struct tftp_session *spt)
{
  spt->in_use = 0;
}

static int tftp_session_allocate(struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &tftp_sessions[k];

    if (!spt->in_use)
        goto found;

    /* sessions time out after 5 inactive seconds */
    if ((int)(curtime - spt->timestamp) > 5000)
        goto found;
  }

  return -1;

 found:
  memset(spt, 0, sizeof(*spt));
  memcpy(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip));
  spt->client_port = tp->udp.uh_sport;

  tftp_session_update(spt);

  return k;
}

static int tftp_session_find(struct tftp_t *tp)
{
  struct tftp_session *spt;
  int k;

  for (k = 0; k < TFTP_SESSIONS_MAX; k++) {
    spt = &tftp_sessions[k];

    if (spt->in_use) {
      if (!memcmp(&spt->client_ip, &tp->ip.ip_src, sizeof(spt->client_ip))) {
	if (spt->client_port == tp->udp.uh_sport) {
	  return k;
	}
      }
    }
  }

  return -1;
}

static int tftp_read_data(struct tftp_session *spt, u_int16_t block_nr,
			  u_int8_t *buf, int len)
{
  int fd;
  int bytes_read = 0;

  char file_path[sizeof(pcempath) + 5 + sizeof(spt->filename)];
  strcpy(file_path, pcempath);
  strcat(file_path, "tftp/");
  strcat(file_path, spt->filename);

  fd = open(file_path, O_RDONLY | O_BINARY);

  if (fd < 0) {
    return -1;
  }

  if (len) {
    lseek(fd, block_nr * 512, SEEK_SET);

    bytes_read = read(fd, buf, len);
  }

  close(fd);

  return bytes_read;
}

static int tftp_send_error(struct tftp_session *spt, 
			   u_int16_t errorcode, const char *msg,
			   struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct SLIRPmbuf *m;
  struct tftp_t *tp;
  int nobytes;

  m = m_get();

  if (!m) {
    return -1;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += if_maxlinkhdr;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);
  
  tp->tp_op = htons(TFTP_ERROR);
  tp->x.tp_error.tp_error_code = htons(errorcode);
  strncpy((char *)tp->x.tp_error.tp_msg, msg, sizeof(tp->x.tp_error.tp_msg));
  tp->x.tp_error.tp_msg[sizeof(tp->x.tp_error.tp_msg)-1] = 0;

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  nobytes = 2;

  m->m_len = sizeof(struct tftp_t) - 514 + 3 + strlen(msg) - 
        sizeof(struct ip) - sizeof(struct udphdr);

  udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

  tftp_session_terminate(spt);

  return 0;
}

static int tftp_send_data(struct tftp_session *spt, 
			  u_int16_t block_nr,
			  struct tftp_t *recv_tp)
{
  struct sockaddr_in saddr, daddr;
  struct SLIRPmbuf *m;
  struct tftp_t *tp;
  int nobytes;

  if (block_nr < 1) {
    return -1;
  }

  m = m_get();

  if (!m) {
    return -1;
  }

  memset(m->m_data, 0, m->m_size);

  m->m_data += if_maxlinkhdr;
  tp = (void *)m->m_data;
  m->m_data += sizeof(struct udpiphdr);
  
  tp->tp_op = htons(TFTP_DATA);
  tp->x.tp_data.tp_block_nr = htons(block_nr);

  saddr.sin_addr = recv_tp->ip.ip_dst;
  saddr.sin_port = recv_tp->udp.uh_dport;

  daddr.sin_addr = spt->client_ip;
  daddr.sin_port = spt->client_port;

  nobytes = tftp_read_data(spt, block_nr - 1, tp->x.tp_data.tp_buf, 512);

  if (nobytes < 0) {
    m_free(m);

    /* send "file not found" error back */

    tftp_send_error(spt, 1, "File not found", tp);

    return -1;
  }

  m->m_len = sizeof(struct tftp_t) - (512 - nobytes) - 
        sizeof(struct ip) - sizeof(struct udphdr);

  udp_output2(NULL, m, &saddr, &daddr, IPTOS_LOWDELAY);

  if (nobytes == 512) {
    tftp_session_update(spt);
  }
  else {
    tftp_session_terminate(spt);
  }

  return 0;
}

static void tftp_handle_rrq(struct tftp_t *tp, int pktlen)
{
  struct tftp_session *spt;
  int s, k, n;
  u_int8_t *src, *dst;

  s = tftp_session_allocate(tp);

  if (s < 0) {
    return;
  }

  spt = &tftp_sessions[s];

  src = tp->x.tp_buf;
  dst = (u_int8_t *)spt->filename;
  n = pktlen - ((uint8_t *)&tp->x.tp_buf[0] - (uint8_t *)tp);

  /* get name */

  for (k = 0; k < n; k++) {
    if (k < TFTP_FILENAME_MAX) {
      dst[k] = src[k];
    }
    else {
      return;
    }
    
    if (src[k] == '\0') {
      break;
    }
  }
      
  if (k >= n) {
    return;
  }
  
  k++;
  
  /* check mode */
  if ((n - k) < 6) {
    return;
  }
  
  if (memcmp(&src[k], "octet\0", 6) != 0) {
      tftp_send_error(spt, 4, "Unsupported transfer mode", tp);
      return;
  }

  pclog("tftp request: %s\n", spt->filename);

  /* do sanity checks on the filename */

  if (strstr(spt->filename, "../") || strstr(spt->filename, "..\\")) {
      tftp_send_error(spt, 2, "Access violation", tp);
      return;
  }

  /* check if the file exists */
  
  if (tftp_read_data(spt, 0, (u_int8_t *)spt->filename, 0) < 0) {
      tftp_send_error(spt, 1, "File not found", tp);
      return;
  }

  tftp_send_data(spt, 1, tp);
}

static void tftp_handle_ack(struct tftp_t *tp, int pktlen)
{
  int s;

  s = tftp_session_find(tp);

  if (s < 0) {
    return;
  }

  if (tftp_send_data(&tftp_sessions[s], 
		     ntohs(tp->x.tp_data.tp_block_nr) + 1, 
		     tp) < 0) {
    return;
  }
}

void tftp_input(struct SLIRPmbuf *m)
{
  struct tftp_t *tp = (struct tftp_t *)m->m_data;

  switch(ntohs(tp->tp_op)) {
  case TFTP_RRQ:
    tftp_handle_rrq(tp, m->m_len);
    break;

  case TFTP_ACK:
    tftp_handle_ack(tp, m->m_len);
    break;
  }
}
