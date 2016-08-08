// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LWIP_ETHERNETIF_H
#define LWIP_ETHERNETIF_H

#include "lwip/netif.h"

err_t ethernetif_init(struct netif *netif);

#endif /* LWIP_ETHERNETIF_H */
