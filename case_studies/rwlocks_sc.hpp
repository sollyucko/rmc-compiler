// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RWLOCKS_SC_H
#define RWLOCKS_SC_H

#include <atomic>
#include <utility>

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
        uintptr_t locked = locked_.fetch_add(1);
        if (!writeLocked(locked)) return;

        // That didn't work. Back off and do the slowpath.
        locked_.fetch_sub(1);
        for (;;) {
            if (!writeLocked(locked)) {
                if (locked_.compare_exchange_weak(locked, locked+1)) return;
            } else {
                locked = locked_;
            }
        }
    }
    void unlock_shared() {
        locked_.fetch_sub(1);
    }

    void lock() {
        uintptr_t locked;
        for (;;) {
            locked = locked_;
            if (!writeLocked(locked)) {
                if (locked_.compare_exchange_weak(locked,
                                                  locked|kWriterLocked)) {
                    break;
                }
            }
        }
        while (readLocked(locked)) {
            locked = locked_;
        }
    }

    void unlock() {
        locked_.fetch_xor(kWriterLocked);
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
        uintptr_t locked = locked_.fetch_add(1);
        while (writeLocked(locked)) {
            locked = locked_;
        }
    }
    void unlock_shared() {
        locked_.fetch_sub(1);
    }

    void lock() {
        for (;;) {
            uintptr_t locked = locked_;
            if (locked == 0) {
                if (locked_.compare_exchange_weak(locked, kWriterLocked)) {
                    break;
                }
            }
        }
    }

    void unlock() {
        locked_.fetch_xor(kWriterLocked);
    }
};

}

#endif
