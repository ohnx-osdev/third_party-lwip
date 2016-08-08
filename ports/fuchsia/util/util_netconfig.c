// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "netif/ethernetif.h"

#include "util/util_netconfig.h"

static struct netif netif;

static int
parse_addr4(const char *addr_str, ip_addr_t *addr)
{
  int num[4];
  int i;
  const char *p = addr_str;

  for (i = 0; i < 4; i++) {
    num[i] = 0;
    if (*p < '0' || *p > '9') {
      /* parse error */
      return -1;
    }
    do {
      num [i] = num[i] * 10 + (*p - '0');
      p++;
    } while ('0' <= *p && *p <= '9');
    if (num[i] > 255) {
      /* parse error */
      return -1;
    }
    if ((i <= 2 && *p == '.') ||
        (i == 3 && *p == '\0')) {
      p++;
    } else {
      /* parse error */
      return -1;
    }
  }
  IP_ADDR4(addr, num[0],num[1],num[2],num[3]);
  return 0;
}

static void
tcpip_init_done(void *arg)
{
  sys_sem_t *sem;
  sem = (sys_sem_t *)arg;
  sys_sem_signal(sem);
}

int lwip_util_netconfig(const char *ipaddr_str, const char *netmask_str,
                         const char *gateway_str, const char *dns_str)
{
  ip_addr_t ipaddr;
  ip_addr_t netmask;
  ip_addr_t gateway;
  ip_addr_t dns;
  int set_dns = 0;

  if (ipaddr_str == NULL) {
    printf("ip address cannot be NULL\n");
    return -1;
  }
  if (parse_addr4(ipaddr_str, &ipaddr) < 0) {
    printf("failed to parse ip address\n");
    return -1;
  }
  if (netmask_str == NULL) {
    printf("netmask cannot be NULL\n");
    return -1;
  }
  if (parse_addr4(netmask_str, &netmask) < 0) {
    printf("failed to parse netmask\n");
    return -1;
  }
  if (gateway_str == NULL) {
    printf("gateway address cannot be NULL\n");
    return -1;
  }
  if (parse_addr4(gateway_str, &gateway) < 0) {
    printf("failed to parse gateway address\n");
    return -1;
  }
  if (dns_str != NULL) {
    if (parse_addr4(dns_str, &dns) < 0) {
      printf("failed to parse dns server address\n");
      return -1;
    }
    set_dns = 1;
  }

  srand((unsigned int)time(0));

  sys_sem_t sem;

  if(sys_sem_new(&sem, 0) != ERR_OK) {
    LWIP_ASSERT("failed to create semaphore", 0);
  }
  tcpip_init(tcpip_init_done, &sem);
  sys_sem_wait(&sem);
  sys_sem_free(&sem);

  netif_set_default(netif_add(&netif,
                              ip_2_ip4(&ipaddr),
                              ip_2_ip4(&netmask),
                              ip_2_ip4(&gateway),
                              NULL,
                              ethernetif_init,
                              tcpip_input));
  netif_set_up(&netif);
  netif_create_ip6_linklocal_address(&netif, 1);

  if (set_dns)
    dns_setserver(0, &dns);

  return 0;
}
