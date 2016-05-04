#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <cassert>
#include <atomic>
#include <stdio.h>

#include "util.hpp"
#include "condvar.hpp"

using namespace rmclib;

// This is total garbage and needs to be a real test.

std::atomic<bool> go;
std::atomic<bool> ready2;
std::atomic<bool> signaled;

std::mutex lock;
condition_variable_futex cvar;

void work(int work = 50) {
    volatile int nus = 0;
    for (int i = 0; i < work; i++) {
        nus++;
    }
}


void t1() {
    while (!go) {}

    work(rand() % 5000);

    std::unique_lock<std::mutex> lk(lock);
    while (!signaled) {
        cvar.wait(lk);
        printf("wakeup\n");
    }
    printf("out\n");
}

void t2() {
    ready2 = true;
    while (!go) {}

    work(rand() % 5000);

    std::unique_lock<std::mutex> lk(lock);
    signaled = true;
    cvar.notify_one();
    printf("signaled\n");
}

void dumbTest() {
    go = ready2 = signaled = 0;

    std::vector<std::thread> threads;

    threads.push_back(std::thread(t1));
    threads.push_back(std::thread(t2));

    while (!ready2) {}
    go = true;
    joinAll(threads);
}

int main(int argc, char** argv) {
    for (int i = 0; i < 50; i++) {
        dumbTest();
    }
    printf("done with main test\n");
}