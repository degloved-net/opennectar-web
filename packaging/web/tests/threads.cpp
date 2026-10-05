#include "Dolphin/os.h"
#include "web/pc_web_threads.h"
#include <emscripten/emscripten.h>
#include <emscripten/stack.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

static OSThread mainThread;
extern "C" {
OSThread* __OSCurrentThread = &mainThread;
void OSPanic(const char* file, int line, const char* format, ...) {
    std::fprintf(stderr, "%s:%d: ", file, line);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::abort();
}
}

namespace {
OSThread first, second, child;
OSMessageQueue queue;
OSMessage buffer[1];
OSMutex mutex;
OSCond condition;
std::vector<int> events;
bool notified = false;

void create(OSThread* thread, OSThreadStartFunction entry) {
    assert(OSCreateThread(thread, entry, nullptr, nullptr, 0, 15, 0));
    assert(OSIsThreadSuspended(thread));
    OSResumeThread(thread);
}
void* yielding(void*) {
    assert(emscripten_stack_get_current() % 16 == 0);
    assert(OSGetCurrentThread() == &first);
    events.push_back(1);
    OSYieldThread();
    assert(emscripten_stack_get_current() % 16 == 0);
    assert(OSGetCurrentThread() == &first);
    events.push_back(2);
    return reinterpret_cast<void*>(42);
}
void* receive(void*) {
    OSMessage value;
    assert(OSReceiveMessage(&queue, &value, OS_MESSAGE_BLOCK));
    events.push_back(static_cast<int>(reinterpret_cast<uintptr_t>(value)));
    assert(OSReceiveMessage(&queue, &value, OS_MESSAGE_BLOCK));
    events.push_back(static_cast<int>(reinterpret_cast<uintptr_t>(value)));
    return nullptr;
}
void* send(void*) {
    assert(OSSendMessage(&queue, reinterpret_cast<void*>(7), OS_MESSAGE_BLOCK));
    assert(OSSendMessage(&queue, reinterpret_cast<void*>(8), OS_MESSAGE_BLOCK));
    return nullptr;
}
void* conditionWaiter(void*) {
    OSLockMutex(&mutex);
    OSLockMutex(&mutex);
    while (!notified) OSWaitCond(&condition, &mutex);
    assert(mutex.count == 2 && mutex.thread == &first);
    OSYieldThread();
    OSUnlockMutex(&mutex);
    OSUnlockMutex(&mutex);
    events.push_back(1);
    return nullptr;
}
void* conditionNotifier(void*) {
    assert(emscripten_stack_get_current() % 16 == 0);
    OSLockMutex(&mutex);
    notified = true;
    OSSignalCond(&condition);
    OSUnlockMutex(&mutex);
    OSYieldThread();
    OSLockMutex(&mutex);
    events.push_back(2);
    OSUnlockMutex(&mutex);
    return nullptr;
}
void* childEntry(void*) {
    assert(emscripten_stack_get_current() % 16 == 0);
    events.push_back(3);
    OSYieldThread();
    events.push_back(4);
    return reinterpret_cast<void*>(99);
}
void* parentEntry(void*) {
    create(&child, childEntry);
    void* result;
    assert(OSJoinThread(&child, &result));
    assert(result == reinterpret_cast<void*>(99));
    events.push_back(5);
    return nullptr;
}
void* waitingForever(void*) {
    OSMessage message;
    OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK);
    std::abort();
}
}

int main() {
    assert(EM_ASM_INT({ return HEAPU8.buffer instanceof ArrayBuffer; }));
    create(&first, yielding);
    pc_web_run_ready_threads();
    assert((events == std::vector<int>{1}));
    assert(OSGetCurrentThread() == &mainThread);
    void* result;
    assert(OSJoinThread(&first, &result));
    assert(result == reinterpret_cast<void*>(42));
    assert((events == std::vector<int>{1, 2}));

    events.clear();
    OSInitMessageQueue(&queue, buffer, 1);
    create(&first, receive);
    pc_web_run_ready_threads();
    assert(first.state == OS_THREAD_STATE_WAITING);
    assert(OSSendMessage(&queue, reinterpret_cast<void*>(7), OS_MESSAGE_NOBLOCK));
    assert(!OSSendMessage(&queue, reinterpret_cast<void*>(8), OS_MESSAGE_NOBLOCK));
    assert(OSSendMessage(&queue, reinterpret_cast<void*>(8), OS_MESSAGE_BLOCK));
    assert(OSJoinThread(&first, nullptr));
    assert((events == std::vector<int>{7, 8}));

    OSInitMessageQueue(&queue, buffer, 1);
    create(&first, send);
    OSMessage message;
    assert(OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK));
    assert(message == reinterpret_cast<void*>(7));
    assert(OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK));
    assert(message == reinterpret_cast<void*>(8));
    assert(OSJoinThread(&first, nullptr));
    assert(!OSReceiveMessage(&queue, &message, OS_MESSAGE_NOBLOCK));
    assert(OSJamMessage(&queue, reinterpret_cast<void*>(9), OS_MESSAGE_NOBLOCK));
    assert(OSReceiveMessage(&queue, &message, OS_MESSAGE_NOBLOCK));
    assert(message == reinterpret_cast<void*>(9));

    events.clear();
    OSInitMutex(&mutex);
    OSInitCond(&condition);
    create(&first, conditionWaiter);
    create(&second, conditionNotifier);
    assert(OSJoinThread(&first, nullptr));
    assert(OSJoinThread(&second, nullptr));
    assert((events == std::vector<int>{1, 2}));

    events.clear();
    create(&first, parentEntry);
    assert(OSJoinThread(&first, nullptr));
    assert((events == std::vector<int>{3, 4, 5}));

    create(&first, waitingForever);
    pc_web_run_ready_threads();
    OSCancelThread(&first);
    assert(OSIsThreadTerminated(&first));
    assert(OSJoinThread(&first, nullptr));
    events.clear();
    create(&first, yielding);
    assert(OSJoinThread(&first, &result));
    assert((events == std::vector<int>{1, 2}));
    std::puts("single-threaded web scheduler tests passed");
}
