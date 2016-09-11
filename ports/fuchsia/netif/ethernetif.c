// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <dirent.h>

#include <mxio/io.h>

#include "lwip/opt.h"

#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"

#include "netif/ethernetif.h"

/* Define those to better describe your network interface. */
#define IFNAME0 'e'
#define IFNAME1 'n'

struct ethernetif {
  /* Add whatever per-interface state that is needed here. */
  int fd;
};

/* Forward declarations. */
static void ethernetif_input(struct netif *netif);
static void ethernetif_thread(void *arg);

/*-----------------------------------------------------------------------------------*/
static void
low_level_init(struct netif *netif)
{
  struct ethernetif *ethernetif;

  DIR* dir;
  struct dirent* de;

  ethernetif = (struct ethernetif *)netif->state;

  if ((dir = opendir("/dev/class/ethernet")) == NULL) {
    printf("ethernetif_init: cannot open /dev/class/ethernet\n");
    return;
  }
  while ((de = readdir(dir)) != NULL) {
    char tmp[128];
    if (de->d_name[0] == '.') {
      continue;
    }
    snprintf(tmp, sizeof(tmp), "/dev/class/ethernet/%s", de->d_name);
    if ((ethernetif->fd = open(tmp, O_RDWR)) >= 0) {
      break;
    }
  }
  LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: fd %d\n", ethernetif->fd));
  closedir(dir);
  if (ethernetif->fd < 0) {
    printf("ethernetif_init: cannot open an ethernet device\n");
    return;
  }

  if (read(ethernetif->fd, netif->hwaddr, 6) != 6) {
    close(ethernetif->fd);
    ethernetif->fd = -1;
    printf("ethernetif_init: cannot read MAC address\n");
    return;
  }
  LWIP_DEBUGF(NETIF_DEBUG,
              ("ethernetif_init: mac: %02x:%02x:%02x:%02x:%02x:%02x\n",
               netif->hwaddr[0], netif->hwaddr[1], netif->hwaddr[2],
               netif->hwaddr[3], netif->hwaddr[4], netif->hwaddr[5]));
  netif->hwaddr_len = 6;

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;

  netif_set_link_up(netif);

  sys_thread_new("ethernetif_thread", ethernetif_thread, netif, DEFAULT_THREAD_STACKSIZE, DEFAULT_THREAD_PRIO);
}

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct ethernetif *ethernetif = (struct ethernetif *)netif->state;
  char buf[1514]; /* MTU (1500) + header (14) */
  ssize_t written;

  pbuf_copy_partial(p, buf, p->tot_len, 0);

  /*
   * Pad spaces toward the minimum packet size (60 bytes without FCS)
   * as the driver doesn't do it for us.
   */
#define MIN_WRITE_SIZE 60

  int write_len = p->tot_len;
  if (write_len < MIN_WRITE_SIZE) {
    memset(buf + write_len, 0, MIN_WRITE_SIZE - write_len);
    write_len = MIN_WRITE_SIZE;
  }
  written = write(ethernetif->fd, buf, write_len);
  if (written == -1) {
    MIB2_STATS_NETIF_INC(netif, ifoutdiscards);
    printf("ethernetif_output: write %d bytes returned -1\n", p->tot_len);
  }
  else {
    MIB2_STATS_NETIF_ADD(netif, ifoutoctets, written);
  }
  return ERR_OK;
}

#define TIMER_MS(n) (((uint64_t)(n)) * 1000000ULL)

static struct pbuf *
low_level_input(struct netif *netif)
{
  struct pbuf *p;
  int len;
  /* TODO(toshik)
   * The driver expects at least 2048 bytes for the buffer size. Reading less
   * than that would fail (Currently 2048 is a magic number)
   */
  char buf[2048];
  struct ethernetif *ethernetif = (struct ethernetif *)netif->state;

  len = read(ethernetif->fd, buf, sizeof(buf));
  if (len < 0) {
    /* TODO(toshik)
     * Currently read() often fails because ethernetif_input() is called even
     * if the fd is not readable.
     */
    /* LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: read returned %d\n", len)); */
    return NULL;
  }

  MIB2_STATS_NETIF_ADD(netif, ifinoctets, len);

  /* We allocate a pbuf chain of pbufs from the pool. */
  p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
  if (p != NULL) {
    pbuf_take(p, buf, len);
    /* acknowledge that packet has been read(); */
  } else {
    /* drop packet(); */
    MIB2_STATS_NETIF_INC(netif, ifindiscards);
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: could not allocate pbuf\n"));
  }

  return p;
}

static void
ethernetif_input(struct netif *netif)
{
  struct pbuf *p = low_level_input(netif);

  if (p == NULL) {
    /* TODO(toshik)
     * Currently low_level_input() may return NULL often because
     * ethernetif_input() is called even if the fd is not readable.
     * Disable the following code for now.
     */
#if 0
#if LINK_STATS
    LINK_STATS_INC(link.recv);
#endif /* LINK_STATS */
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: low_level_input returned NULL\n"));
#endif
    return;
  }

  if (netif->input(p, netif) != ERR_OK) {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: netif input error\n"));
    pbuf_free(p);
  }
}

err_t
ethernetif_init(struct netif *netif)
{
  struct ethernetif *ethernetif = (struct ethernetif *)mem_malloc(sizeof(struct ethernetif));

  if (ethernetif == NULL) {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: out of memory for ethernetif\n"));
    return ERR_MEM;
  }
  netif->state = ethernetif;
  MIB2_INIT_NETIF(netif, snmp_ifType_other, 100000000);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  netif->output = etharp_output;
  netif->output_ip6 = ethip6_output;
  netif->linkoutput = low_level_output;
  netif->mtu = 1500;

  low_level_init(netif);

  return ERR_OK;
}

/*-----------------------------------------------------------------------------------*/

static void
ethernetif_thread(void *arg)
{
  struct netif *netif;
  struct ethernetif *ethernetif;

  netif = (struct netif *)arg;
  ethernetif = (struct ethernetif *)netif->state;

  while(1) {
    mxio_wait_fd(ethernetif->fd, MXIO_EVT_READABLE, NULL, MX_TIME_INFINITE);
    /* TODO(toshik) mxio_wait_fd() might return even if the fd is not readable.
     * we should check why it returned, but it is not possible as errno is not
     * set currently.
     */
    ethernetif_input(netif);
  }
}
