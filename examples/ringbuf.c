#include "atomic.h"
#include "rmc.h"


/* Lockfree ringbuffers: works if there is at most one writer,
 * at most one reader at a time. */
/* see https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/circular-buffers.txt
 * and https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/tree/Documentation/memory-barriers.txt
 * */

#define KBD_BUF_SIZE (1024*1)



/* Generic things */
typedef struct ring_buf_t {
    unsigned char buf[KBD_BUF_SIZE];
    unsigned front, back;
} ring_buf_t;

#define ring_inc(v) (((v) + 1) % KBD_BUF_SIZE)

/**** Something like my original ringbuf code from my 15-410 kernel **********/
/* Doesn't bother with any memory model stuff and so is actually wrong. */
int buf_enqueue_410(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = buf->front;

    int enqueued = 0;
    if (ring_inc(back) != front) {
        buf->buf[back] = c;
        buf->back = ring_inc(back);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_410(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = buf->back;

    int c = -1;
    if (front != back) {
        c = buf->buf[front];
        buf->front = ring_inc(front);
    }
    return c;
}

/******** Something that is at least correct wrt the compiler  **********/
int buf_enqueue_compiler_safe(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        ACCESS_ONCE(buf->back) = ring_inc(back);
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_compiler_safe(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }
    return c;
}

/*************************** Linux style ************************************/
/* Does the linux style one work *without* spinlocks under the assumption
 * that the same CPUs are calling enqueue and dequeue?? */

/* Some of the linux documentation here seems nonsensical to me. */

/*
 * I did some thinking about what could go wrong without spinlocks, and
 * convinced myself that the following simplified example was relevant:
 *
 * c = ACCESS_ONCE(clear);      | d = data;
 * if (c) { data = 1; }         | smp_store_release(&clear, 1);
 *
 * We want to know whether it is possible for d == 1 at the end.
 * This models a consumer thread reading data and then indicating
 * that the buffer is clear for more writing. The writing thread
 * checks that the buffer is clear and then writes.
 * This is possible in the C++ memory model (according to CppMem),
 * but seems pretty damn weird. It can't happen on POWER.
 * Maybe on Alpha? It seems generally pretty dubious.
 *
 * The "linux memory model" says that control dependencies can order
 * prior loads against later stores. This might mean that the spin_lock
 * and spin_unlock arne't actually needed under the "linux memory model".
 * Although maybe it being cross function gets weird... Hm.
 * Not really: ARM/POWER won't reorder reads *from the same address*.
 *
 * I think this is sort of equivalent to the queue without spinlocks except
 * that the write would need to get speculated before *two* conditions...
 * That seems super weird.
 * Also, it's not totally clear to me that the RELEASE/ACQUIRE semantics
 * of dropping and taking a lock would save you by spec?
 * (By what spec??)
 * Also there are other barriers happening, so...
 *
 *
 * So also, linux doesn't actually seem to *use* smp_store_release and friends.
 * Cute.
 * See: https://github.com/torvalds/linux/commit/47933ad41a86a4a9b50bed7c9b9bd2ba242aac63
 *
 * The linux documentation call produce_item() and consume_item() functions
 * to do the actually accesses. I made them ACCESS_ONCE; I'm not totally sure
 * if that is necessary.
 *
 */

#define spin_lock(x) (void)0
#define spin_unlock(x) (void)0

int buf_enqueue_linux(ring_buf_t *buf, unsigned char c)
{
    spin_lock(&enqueue_lock);

    unsigned back = buf->back;
    /* Linux claims:
     * "The spin_unlock() and next spin_lock() provide needed ordering." */
    /* I don't think it matters. */
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    spin_unlock(&enqueue_lock);

    return enqueued;
}

int buf_dequeue_linux(ring_buf_t *buf)
{
    spin_lock(&dequeue_lock);

    unsigned front = buf->front;
    unsigned back = smp_load_acquire(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    spin_unlock(&dequeue_lock);
    return c;
}

/********************* Linux/C11 style ************************************/
/* This is more or less what it would be in C11 land but in a Linux style*/
int buf_enqueue_c11(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = smp_load_acquire(&buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_c11(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = smp_load_acquire(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    return c;
}

/******* This is what I could do if I was a "thoroughly bad man" *********/
/* Use consume without having an honest data dependency. */
/* If we were properly using C11 with a proper consume implementation,
 * the compiler should preserve a bogus dependency. We don't, though
 * so we implemented bullshit_dep() with inline assembly on ARM. */

int buf_enqueue_c11_badman(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = smp_load_consume(&buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[bullshit_dep(back, front)]) = c;
        smp_store_release(&buf->back, ring_inc(back));
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_c11_badman(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = smp_load_consume(&buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[bullshit_dep(front, back)]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    return c;
}

/******* Here we use deps for all of our xo edges *********/
/* This actually seems to be *terribad* */
/* Things were actually *faster* when I had a pointless dependency in
 * enqueue */

#define buf_enqueue_depsbad buf_enqueue_linux

int buf_dequeue_depsbad(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[bullshit_dep(front, back)]);
        ctrl_isync(c);
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }

    return c;
}


/* OK, actually, using deps for *all* the xo edges doesn't even work.
 * (Well, not technically.)
 * Using a dep for the first and a dmb for the second seems good.
 * Using a ctrlisb for the second is bad. */

/* I bet that it is important to have *some* dmbs if you are doing
 * writes to make sure that the writes actually ever get flushed
 * out! */

#define buf_enqueue_deps buf_enqueue_linux

int buf_dequeue_deps(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[bullshit_dep(front, back)]);
        vis_barrier();
//        ctrl_isync(c);
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }

    return c;
}

// Slight variation. Which is slow! The ctrlisb variant is
// actually faster this time.
#define buf_enqueue_deps2 buf_enqueue_deps

int buf_dequeue_deps2(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);
//    vis_barrier();
    ctrl_isync(back);

    int c = -1;
    if (front != back) {
        c = ACCESS_ONCE(buf->buf[front]);
        // The second version is lol slow.
        //ACCESS_ONCE(*bullshit_dep(&buf->front, c)) = ring_inc(front);
        ACCESS_ONCE(bullshit_dep(buf, c)->front) = ring_inc(front);
    }

    return c;
}

// Try sinking the isb. It doesn't seem to help.
#define buf_enqueue_sunkisb buf_enqueue_linux


int buf_dequeue_sunkisb(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        isync(); // we have a ctrl dep from back
        c = ACCESS_ONCE(buf->buf[front]);
        smp_store_release(&buf->front, ring_inc(front));
    }

    return c;
}


/****************** This is the old linux version using barriers *************/
/* I dropped the locks but maybe they are needed? I haven't figured this out */
int buf_enqueue_linux_old(ring_buf_t *buf, unsigned char c)
{
    unsigned back = buf->back;
    unsigned front = ACCESS_ONCE(buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        ACCESS_ONCE(buf->buf[back]) = c;

        smp_wmb(); /* commit the item before incrementing the head */

        ACCESS_ONCE(buf->back) = ring_inc(back);
        enqueued = 1;
    }

    return enqueued;
}

int buf_dequeue_linux_old(ring_buf_t *buf)
{
    unsigned front = buf->front;
    unsigned back = ACCESS_ONCE(buf->back);

    int c = -1;
    if (front != back) {
        /* read index before reading contents at that index */
        smp_rmb();

        c = ACCESS_ONCE(buf->buf[front]);

        smp_mb(); /* finish reading descriptor before incrementing tail */
        ACCESS_ONCE(buf->front) = ring_inc(front);
    }

    return c;
}


/******************* try an rmc version..... ****************************/

/* The important things are:
 *
 * * We never read data that hasn't actually been written.
 * insert -vo-> e_update -vo/rf-> check -xo-> read
 * So insert -vt-> read, which means we read the right value.
 *
 * It is important that edges point into subsequent function calls
 * also: any buffer insert is visibility ordered before all subsequent
 * updates of back.
 *
 * * We never trash data:
 * The trashing data example gets kind of tricky, since
 * Imagine that we have a full buffer: back == 0, front == 1
 *
 * We annotate things with what the are reading or writing when
 * it is relevant.
 *
 * dequeue does:
 *   d_check0 -xo-> read0(R buf[1]) -xo-> d_update0(W front = 2)
 *
 *   d_check1 -xo-> read1(R buf[2]) -xo-> d_update1(W front = 3)
 *
 * enqueue does:
 *   e_check0(R front == 2) -xo-> insert0(W buf[0]) -vo-> e_update0
 *        \------------xo--------------v
 *   e_check1(R front == 3) -xo-> insert1(W buf[1]) -vo-> e_update1
 *
 * There are cross function call edges, but we only label the
 * e_check0 -xo-> insert1 edge, since it is the only relevant here.
 *
 * We have d_update0 -rf-> e_check0 and d_update1 -rf-> e_check1.
 * We want to make sure that the value dequeue is dequeueing doesn't
 * get stomped, so we want to *rule out* insert1 -rf-> read0.
 *
 * Combining xo and rf edges, we have the following sequence in trace order:
 * read0 -> d_update0 -> e_check0 -> insert1.
 * Having insert1 -rf-> read0 would cause a cycle in trace order.
 *
 * ----
 *
 * Now consider the (I suspect) optimal compilation for ARM:
 * For the intra-call stuff:
 * put a sync between insert and e_update, put addr dependencies between
 * dcheck -> read, read -> d_update. insert is a store, so the
 * ctrl dependency from e_check orders it. Cool.
 *
 * Now we need to consider how we get the edges to subsequent calls:
 * The vo edge is obvious.
 * If we assume buf is always the same...
 * Then because of read/read coherence we get ?_check -xo-> ?_check,
 * which gives us what we need for the edges out of both checks.
 * We also need read0 -xo-> update1, which we get
 * from noting that coherence gives us update0 -xo-> update1.
 *
 * Although actually maybe the coherence stuff does *not* line up
 * properly with our notion of xo.
 *
 *
*/

int buf_enqueue_rmc(ring_buf_t *buf, unsigned char c)
{
    XEDGE(e_check, insert);
    VEDGE(insert, e_update);

    unsigned back = buf->back;
    L(e_check, unsigned front = buf->front);

    int enqueued = 0;
    if (ring_inc(back) != front) {
        L(insert, buf->buf[back] = c);
        L(e_update, buf->back = ring_inc(back));
        enqueued = 1;
    }
    return enqueued;
}

int buf_dequeue_rmc(ring_buf_t *buf)
{
    XEDGE(d_check, read);
    XEDGE(read, d_update);

    unsigned front = buf->front;
    L(d_check, unsigned back = buf->back);

    int c = -1;
    if (front != back) {
        L(read, c = buf->buf[front]);
        L(d_update, buf->front = ring_inc(front));
    }
    return c;
}


#ifndef NO_TEST
/*************************** Testing ***************************************/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * linux (using ctrl+isb) is around 4.4s, tends to overflow
 * linux_old 4.1s
 * it looks like dmb is actually better performing than ctrl+isb!!
 * c11 around 5.7s, tends to underflow
 * c11_badman around 5.0s, tends to underflow
 * depsbad around 6.1s
 *
 * Ok, wait, when I run linux_old now I get like 6s!
 *
 * The new version of deps seems to slightly outperform linux.
 *
 * There is a lot of weird.
 */
#ifndef TEST_NAME
#define TEST_NAME rmc
#endif

/* C. */
#define CAT(x,y)      x ## y
#define XCAT(x,y)     CAT(x,y)

#define buf_enqueue XCAT(buf_enqueue_, TEST_NAME)
#define buf_dequeue XCAT(buf_dequeue_, TEST_NAME)

ring_buf_t test_buf;

#define MODULUS 251

#define ITERATIONS 100000

int PADDED empty_count;
int PADDED full_count;

void *enqueuer(void *p)
{
    long iterations = (long)p;
    int i = 0;

    while (i < iterations) {
        unsigned char c = i % MODULUS;
        int success = buf_enqueue(&test_buf, c);
        if (!success) {
            full_count++;
        } else {
            i++;
        }
    }

    return NULL;
}


void *dequeuer(void *p)
{
    long iterations = (long)p;
    int i = 0;

    while (i < iterations) {
        int c = buf_dequeue(&test_buf);
        if (c < 0) {
            empty_count++;
        } else {
            unsigned char expected = i % MODULUS;
            if (c != expected) {
                printf("wrong! expected=%d, got=%d\n", expected, c);
            }
            i++;
        }
    }

    return NULL;
}

int main(int argc, char **argv)
{
    long iterations = (argc == 2) ? strtol(argv[1], NULL, 0) : ITERATIONS;
    void *arg = (void *)(long)iterations;

    pthread_t enqueue_thread, dequeue_thread;
    pthread_create(&enqueue_thread, NULL, enqueuer, arg);
    pthread_create(&dequeue_thread, NULL, dequeuer, arg);

    pthread_join(enqueue_thread, NULL);
    pthread_join(dequeue_thread, NULL);

    printf("empty = %d, full = %d\n", empty_count, full_count);

    return 0;
}
#endif