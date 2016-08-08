// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LWIP_UTIL_NETCONFIG_H
#define LWIP_UTIL_NETCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

extern int lwip_util_netconfig(const char *ipaddr,
                               const char *netmask,
                               const char *gateway,
                               const char *dns);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_UTIL_NETCONFIG_H */
