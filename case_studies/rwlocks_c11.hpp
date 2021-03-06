// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RWLOCKS_C11_H
#define RWLOCKS_C11_H

#include <atomic>
#include <utility>
#include "util.hpp"

namespace rmclib {

// This can starve readers but not writers, I think
class busy_rwlock_readstarve {
    const uintptr_t kWriterLocked = ~(~0u >> 1);
    std::atomic<uintptr_t> locked_{0};

    void delay() { }
    bool writeLocked(uintptr_t locked) { return (locked & kWriterLocked) != 0; }
    bool readLocked(uintptr_t locked) { return (locked & ~kWriterLocked) != 0; }

public:
    void lock_shared() {
        // Optimistic lock attempt
        uintptr_t locked = locked_.fetch_add(1, mo_acq);
        if (!writeLocked(locked)) return;

        // That didn't work. Back off and do the slowpath.
        locked_.fetch_sub(1, mo_rlx); // XXX ??
        for (;;) {
            if (!writeLocked(locked)) {
                if (locked_.compare_exchange_weak(locked, locked+1,
                                                  mo_acq, mo_rlx)) return;
            } else {
                locked = locked_.load(mo_rlx);
            }
        }
    }
    void unlock_shared() {
        locked_.fetch_sub(1, mo_rel);
    }

    void lock() {
        uintptr_t locked;
        for (;;) {
            locked = locked_.load(mo_rlx);
            if (!writeLocked(locked)) {
                if (locked_.compare_exchange_weak(
                        locked, locked|kWriterLocked, mo_acq, mo_rlx)) {
                    break;
                }
            }
        }
        while (readLocked(locked)) {
            locked = locked_.load(mo_acq);
        }
    }

    void unlock() {
        locked_.fetch_xor(kWriterLocked, mo_rel);
    }
};

class busy_rwlock_writestarve {
    const uintptr_t kWriterLocked = ~(~0u >> 1);
    std::atomic<uintptr_t> locked_{0};

    void delay() { }
    bool writeLocked(uintptr_t locked) { return (locked & kWriterLocked) != 0; }
    bool readLocked(uintptr_t locked) { return (locked & ~kWriterLocked) != 0; }

public:
    void lock_shared() {
        uintptr_t locked = locked_.fetch_add(1, mo_acq);
        while (writeLocked(locked)) {
            locked = locked_.load(mo_acq);
        }
    }
    void unlock_shared() {
        locked_.fetch_sub(1, mo_rel);
    }

    void lock() {
        for (;;) {
            uintptr_t locked = locked_.load(mo_rlx);
            if (locked == 0) {
                if (locked_.compare_exchange_weak(locked, kWriterLocked)) {
                    break;
                }
            }
        }
    }

    void unlock() {
        locked_.fetch_xor(kWriterLocked, mo_rel);
    }
};

}

#endif
