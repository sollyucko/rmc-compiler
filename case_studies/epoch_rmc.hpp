// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_EPOCH_RMC
#define RMC_EPOCH_RMC

#include <rmc++.h>

#define UnsafeTStack EpochGarbageStack
#include "unsafe_tstack_rmc.hpp"

namespace rmclib {
    template<class T> using epoch_atomic = rmc::atomic<T>;
}
#include "epoch_shared.hpp"

#endif
