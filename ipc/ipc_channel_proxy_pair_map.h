// Copyright (c) 2014 Intel Corp. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_IPC_CHANNEL_PROXY_PAIR_MAP_H_
#define IPC_IPC_CHANNEL_PROXY_PAIR_MAP_H_

#include <map>
#include <string>

#include "base/basictypes.h"
#include "base/memory/singleton.h"
#include "base/synchronization/lock.h"

namespace IPC {

class ChannelProxy;

struct ChannelProxyPair {
  ChannelProxyPair() : server(NULL), client(NULL) {}
  ChannelProxy* server;
  ChannelProxy* client;
};

// A global map for server and client channel proxy.
class ChannelProxyPairMap {
 public:
  static ChannelProxyPairMap* GetInstance();

  bool HasPair(const std::string& id);

  void RemovePair(const std::string& id);

  ChannelProxyPair& GetPair(const std::string& id);

 private:
  ChannelProxyPairMap();

  friend struct DefaultSingletonTraits<ChannelProxyPairMap>;

  std::map<std::string, ChannelProxyPair> pair_map_;
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(ChannelProxyPairMap);
};

}  // namespace IPC

#endif  // IPC_IPC_CHANNEL_PROXY_PAIR_MAP_H_
