#include <stddef.h>
#include "atomic.h"
#include "rmc.h"

///////////////

#define container_of(ptr, type, member) ({                              \
            const typeof( ((type *)0)->member ) *__mptr = (ptr);        \
            (type *)( (char *)__mptr - offsetof(type,member) );})


typedef struct list_node_t {
    struct list_node_t *next;
    struct list_node_t *prev;
} list_node_t;
typedef struct list_head_t {
    list_node_t head;
} list_head_t;

//

#ifdef USE_ACQUIRE
#define rcu_dereference(p) smp_load_acquire(&p)
#else
#define rcu_dereference(p) smp_load_consume(&p)
#endif

#define list_entry_rcu_linux(ptr, type, member) \
    container_of(rcu_dereference(ptr), type, member)

#define list_for_each_entry_rcu_linux(pos, head, member) \
    for (pos = list_entry_rcu_linux((head)->head.next, typeof(*pos), member); \
         &pos->member != &(head)->head; \
         pos = list_entry_rcu_linux(pos->member.next, typeof(*pos), member))


#define list_entry_rcu_rmc(ptr, type, member, tag)  \
    container_of(L(tag, ptr), type, member)


#define list_for_each_entry_rcu_rmc2(pos, head, member, tag_a, tag_b) \
    XEDGE(tag_a, tag_a); XEDGE(tag_a, tag_b); \
    for (pos = list_entry_rcu_rmc((head)->head.next, typeof(*pos), \
                                  member, tag_a); \
         &pos->member != &(head)->head; \
         pos = list_entry_rcu_rmc(pos->member.next, typeof(*pos), member,tag_a))

#define list_for_each_entry_rcu_rmc(pos, head, member, tag) \
    list_for_each_entry_rcu_rmc2(pos, head, member, __rcu_read, tag)


/////////////////


#define rcu_read_lock() do { } while (0)
#define rcu_read_unlock() LS(__dummy_awful_hack, PUSH)


/////
typedef struct noob_node_t {
    struct noob_node_t *next;
    int key;
    int val;
} noob_node_t;

typedef struct test_node_t {
    int key;
    int val;
    list_node_t l;
} test_node_t;



////////////

int noob_search_rmc(noob_node_t **head, int key) {
    int res = -1;
    XEDGE(a, b);
    XEDGE(a, a);

    rcu_read_lock();
    for (noob_node_t *node = L(a, *head); node; node = L(a, node->next)) {
        if (L(b, node->key) == key) {
            res = L(b, node->val);
            break;
        }
    }

    rcu_read_unlock();
    return res;
}


int list_search_rmc(list_head_t *head, int key) {
    int res = -1;
    test_node_t *node;
    rcu_read_lock();
    list_for_each_entry_rcu_rmc(node, head, l, r) {
        if (L(r, node->key) == key) {
            res = L(r, node->val);
            break;
        }
    }

    rcu_read_unlock();
    return res;
}

//

int list_search_linux(list_head_t *head, int key) {
    int res = -1;
    test_node_t *node;
    //rcu_read_lock();
    list_for_each_entry_rcu_linux(node, head, l) {
        if (node->key == key) {
            res = node->val;
            break;
        }
    }

    //rcu_read_unlock();
    return res;
}



#ifndef NO_TEST
/*************************** Testing ***************************************/
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

void __list_insert_between(list_node_t *n, list_node_t *n1, list_node_t *n2) {
    n->prev = n1;
    n->next = n2;
    n1->next = n;
    n2->prev = n;
}
void list_insert_before(list_node_t *n, list_node_t *n_old) {
    __list_insert_between(n, n_old->prev, n_old);
}
void list_insert_tail(list_node_t *n, list_head_t *head) {
    list_insert_before(n, &head->head);
}

//
void build_list(list_head_t *head, int size) {
    head->head.next = head->head.prev = &head->head;
    for (int i = 0; i < size; i++) {
        test_node_t *node = malloc(sizeof(*node));
        assert(node);

        node->key = i;
        node->val = -i;
        list_insert_tail(&node->l, head);
    }
}
//

#define list_search list_search_linux

#define INT_TO_PTR(i) ((void *)(long)i)
#define PTR_TO_INT(p) ((long)p)


long iterations = 10000000;
long size = 50;
long num_threads = 1;

list_head_t head;


void *tester(void *p)
{
    long *data = p;
    unsigned seed = 410 + PTR_TO_INT(p);

    for (int i = 0; i < iterations; i++) {
        int idx = rand_r(&seed) % size;
        int res = list_search(&head, idx);
        assert(idx == -res);
    }

    return NULL;
}



int main(int argc, char **argv)
{
    int opt;
    while ((opt = getopt(argc, argv, "i:s:t:")) != -1) {
        switch (opt) {
        case 'i': iterations = strtol(optarg, NULL, 0); break;
        case 's': size = strtol(optarg, NULL, 0); break;
        case 't': num_threads = strtol(optarg, NULL, 0); break;
        }
    }

    build_list(&head, size);
    smp_mb();

    pthread_t threads[num_threads]; // weeeee, dangerous
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, tester, INT_TO_PTR(i));
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}




#endif
