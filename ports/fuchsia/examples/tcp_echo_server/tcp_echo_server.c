// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "lwip/sockets.h"
#include "util/util_netconfig.h"
#include "util/util_debug.h"

#define ECHO_SERVICE_PORT 7

void tcp_echo_server(short int port) {
  int s = lwip_socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (lwip_bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    printf("bind failed\n");
    goto end;
  }

  if (lwip_listen(s, 1) < 0) {
    printf("listen failed\n");
    goto end;
  }

  printf("waiting for a connection...\n");
  int conn = lwip_accept(s, NULL, NULL);
  if (conn < 0) {
    printf("accept failed\n");
    goto end;
  }
  printf("connected\n");

  while (1) {
    char buf[1024];
    int n;
    n = lwip_read(conn, buf, sizeof(buf));
    if (n < 0) {
      printf("read failed\n");
      break;
    } else if (n == 0) {
      break;
    } else {
      printf("echo %d bytes\n", n);
      lwip_write(conn, buf, n);
    }
  }

  lwip_close(conn);
 end:
  lwip_close(s);
}

int main(int argc, char** argv) {
#if defined(LWIP_DEBUG)
  if (argc > 1) {
    if (argv[1][0] == '-' && argv[1][1] == 'd') {
      lwip_util_debug_on();
      --argc;
      ++argv;
    }
  }
#endif
  if (argc < 4) {
    printf("usage: tcp_echo_server ip_addr netmask gateway\n");
    return 1;
  }

  if (lwip_util_netconfig(argv[1], argv[2], argv[3], NULL) < 0)
    return 1;

  tcp_echo_server(ECHO_SERVICE_PORT);

  return 0;
}
