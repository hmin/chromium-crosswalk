// Copyright (c) 2014 Intel Corp. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/ipc_channel_proxy_pair_map.h"

#include "ipc/ipc_channel_proxy.h"

namespace IPC {

ChannelProxyPairMap::ChannelProxyPairMap() {
}

void ChannelProxyPairMap::RemovePair(const std::string& id) {
  base::AutoLock lock(lock_);
  pair_map_.erase(id);
}

bool ChannelProxyPairMap::HasPair(const std::string& id) {
  base::AutoLock lock(lock_);
  return pair_map_.find(id) != pair_map_.end();
}

ChannelProxyPair& ChannelProxyPairMap::GetPair(const std::string& id) {
  base::AutoLock lock(lock_);
  return pair_map_[id];
}

// static
ChannelProxyPairMap* ChannelProxyPairMap::GetInstance() {
  return Singleton<ChannelProxyPairMap>::get();
}

}  // namespace IPC
