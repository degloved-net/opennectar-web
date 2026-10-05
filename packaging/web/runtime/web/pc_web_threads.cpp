#include "pc_web_threads.h"
#include "Dolphin/os.h"

#include <emscripten/fiber.h>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include "PikiMallocAllocator.h"

namespace {
constexpr size_t cStackSize = 2 * 1024 * 1024;
constexpr size_t asyncStackSize = 1024 * 1024;
constexpr size_t stackAlignment = 16;

void* alignedStack(unsigned char* storage) {
    const auto address = reinterpret_cast<uintptr_t>(storage);
    return reinterpret_cast<void*>((address + stackAlignment - 1) & ~(uintptr_t(stackAlignment) - 1));
}

struct FreeStack { void operator()(unsigned char* p) const { std::free(p); } };
using Stack = std::unique_ptr<unsigned char, FreeStack>;
Stack allocateStack(size_t bytes) {
    auto* memory = static_cast<unsigned char*>(std::malloc(bytes));
    if (!memory) throw std::bad_alloc();
    return Stack(memory);
}

struct Task {
    static void* operator new(size_t bytes) {
        if (void* p = std::malloc(bytes)) return p;
        throw std::bad_alloc();
    }
    static void operator delete(void* p) { std::free(p); }
    emscripten_fiber_t fiber {};
    OSThread* thread;
    OSThreadStartFunction entry;
    void* argument;
    void* waitKey = nullptr;
    Stack stack = allocateStack(cStackSize + stackAlignment - 1);
    Stack asyncStack = allocateStack(asyncStackSize + stackAlignment - 1);
};

using TaskEntry = std::pair<OSThread* const, std::unique_ptr<Task>>;
std::unordered_map<OSThread*, std::unique_ptr<Task>, std::hash<OSThread*>,
                   std::equal_to<OSThread*>, PikiMallocAllocator<TaskEntry>> tasks;
std::vector<OSThread*, PikiMallocAllocator<OSThread*>> taskOrder;
emscripten_fiber_t mainFiber {};
alignas(16) unsigned char mainAsyncStack[asyncStackSize];
OSThread* mainThread = nullptr;
Task* current = nullptr;
void* mainWaitKey = nullptr;
int schedulerDisabled = 0;

void initialize() {
    if (mainThread) return;
    mainThread = __OSCurrentThread;
    emscripten_fiber_init_from_current_context(&mainFiber, mainAsyncStack, sizeof(mainAsyncStack));
}

void wake(void* key) {
    if (mainWaitKey == key) mainWaitKey = nullptr;
    for (auto& item : tasks) {
        Task& task = *item.second;
        if (task.thread->state == OS_THREAD_STATE_WAITING && task.waitKey == key) {
            task.waitKey = nullptr;
            task.thread->state = OS_THREAD_STATE_READY;
        }
    }
}

void returnToMain() {
    Task* task = current;
    current = nullptr;
    __OSCurrentThread = mainThread;
    emscripten_fiber_swap(&task->fiber, &mainFiber);
}

bool runReady() {
    initialize();
    const auto order = taskOrder;
    bool ran = false;
    for (OSThread* thread : order) {
        auto found = tasks.find(thread);
        if (found == tasks.end() || thread->state != OS_THREAD_STATE_READY || thread->suspend > 0) continue;
        current = found->second.get();
        thread->state = OS_THREAD_STATE_RUNNING;
        __OSCurrentThread = thread;
        emscripten_fiber_swap(&mainFiber, &current->fiber);
        ran = true;
    }
    return ran;
}

void wait(void* key) {
    initialize();
    if (current) {
        current->waitKey = key;
        current->thread->state = OS_THREAD_STATE_WAITING;
        returnToMain();
    } else {
        mainWaitKey = key;
        while (mainWaitKey) {
            if (schedulerDisabled || !runReady()) {
                for (const auto& item : tasks) {
                    const Task& task = *item.second;
                    std::fprintf(stderr, "web task %p entry=%p state=%d suspended=%d wait=%p\n",
                                 static_cast<void*>(task.thread), reinterpret_cast<void*>(task.entry),
                                 task.thread->state, task.thread->suspend, task.waitKey);
                }
                OSPanic(__FILE__, __LINE__, "single-threaded web task deadlock: main wait=%p scheduler=%d", key, schedulerDisabled);
            }
        }
    }
}

void taskEntry(void* raw) {
    Task* task = static_cast<Task*>(raw);
    OSExitThread(task->entry(task->argument));
}
}

void pc_web_run_ready_threads() {
    if (!schedulerDisabled) runReady();
}

extern "C" {
void OSInitThreadQueue(OSThreadQueue* queue) { queue->head = queue->tail = nullptr; }
OSThread* OSGetCurrentThread() { return __OSCurrentThread; }
BOOL OSIsThreadTerminated(OSThread* thread) { return thread->state == OS_THREAD_STATE_MORIBUND; }
s32 OSDisableScheduler() { return schedulerDisabled++; }
s32 OSEnableScheduler() {
    const int previous = schedulerDisabled;
    if (schedulerDisabled) --schedulerDisabled;
    return previous;
}

void OSYieldThread() {
    if (schedulerDisabled) return;
    if (current) {
        current->thread->state = OS_THREAD_STATE_READY;
        returnToMain();
    } else {
        runReady();
    }
}

BOOL OSCreateThread(OSThread* thread, OSThreadStartFunction entry, void* argument,
                    void* stack, u32 stackSize, OSPriority priority, u16 attr) {
    (void)stack; (void)stackSize;
    if (!entry) return FALSE;
    initialize();
    auto previous = tasks.find(thread);
    if (previous != tasks.end() && !OSIsThreadTerminated(thread)) return FALSE;
    auto task = std::make_unique<Task>();
    task->thread = thread;
    task->entry = entry;
    task->argument = argument;
    std::memset(thread, 0, sizeof(*thread));
    thread->state = OS_THREAD_STATE_READY;
    thread->suspend = 1;
    thread->priority = thread->base = priority;
    thread->attr = attr;
    emscripten_fiber_init(&task->fiber, taskEntry, task.get(), alignedStack(task->stack.get()), cStackSize,
                         alignedStack(task->asyncStack.get()), asyncStackSize);
    if (previous == tasks.end()) taskOrder.push_back(thread);
    tasks[thread] = std::move(task);
    return TRUE;
}

void OSExitThread(void* value) {
    if (!current) std::exit(0);
    current->thread->val = value;
    current->thread->state = OS_THREAD_STATE_MORIBUND;
    wake(current->thread);
    returnToMain();
    std::abort();
}

void OSCancelThread(OSThread* thread) {
    if (current && current->thread == thread) OSExitThread(nullptr);
    thread->state = OS_THREAD_STATE_MORIBUND;
    wake(thread);
}
void OSDetachThread(OSThread* thread) { thread->attr |= OS_THREAD_ATTR_DETACH; }
s32 OSResumeThread(OSThread* thread) {
    const s32 previous = thread->suspend;
    if (thread->suspend > 0) --thread->suspend;
    return previous;
}
s32 OSSuspendThread(OSThread* thread) {
    const s32 previous = thread->suspend++;
    if (current && current->thread == thread) {
        thread->state = OS_THREAD_STATE_READY;
        returnToMain();
    }
    return previous;
}
void OSSleepThread(OSThreadQueue* queue) { wait(queue); }
void OSWakeupThread(OSThreadQueue* queue) { wake(queue); }
void OSClearStack(u8 value) { (void)value; }
OSPriority OSGetThreadPriority(OSThread* thread) { return thread->priority; }
BOOL OSIsThreadSuspended(OSThread* thread) { return thread->suspend > 0; }
BOOL OSJoinThread(OSThread* thread, void** value) {
    if (thread == __OSCurrentThread || !tasks.count(thread)) return FALSE;
    while (!OSIsThreadTerminated(thread)) wait(thread);
    if (value) *value = thread->val;
    return TRUE;
}
BOOL OSSetThreadPriority(OSThread* thread, OSPriority priority) {
    thread->priority = thread->base = priority;
    return TRUE;
}
OSThread* OSSetIdleFunction(OSIdleFunction fn, void* parameter, void* stack, u32 size) {
    (void)fn; (void)parameter; (void)stack; (void)size;
    return nullptr;
}
OSThread* OSGetIdleFunction() { return nullptr; }
s32 OSCheckActiveThreads() { return 1; }
void __OSReschedule() { OSYieldThread(); }
OSPriority __OSGetEffectivePriority(OSThread* thread) { return thread->priority; }
void __OSPromoteThread(OSThread* thread, OSPriority priority) { thread->priority = priority; }
OSSwitchThreadCallback OSSetSwitchThreadCallback(OSSwitchThreadCallback callback) { (void)callback; return nullptr; }

void OSInitMutex(OSMutex* mutex) {
    OSInitThreadQueue(&mutex->queue);
    mutex->thread = nullptr;
    mutex->count = 0;
}
BOOL OSTryLockMutex(OSMutex* mutex) {
    if (mutex->thread && mutex->thread != __OSCurrentThread) return FALSE;
    mutex->thread = __OSCurrentThread;
    ++mutex->count;
    return TRUE;
}
void OSLockMutex(OSMutex* mutex) {
    while (!OSTryLockMutex(mutex)) wait(mutex);
}
void OSUnlockMutex(OSMutex* mutex) {
    if (mutex->thread != __OSCurrentThread || mutex->count < 1) return;
    if (--mutex->count == 0) {
        mutex->thread = nullptr;
        wake(mutex);
    }
}
void OSInitCond(OSCond* cond) { OSInitThreadQueue(&cond->queue); }
void OSWaitCond(OSCond* cond, OSMutex* mutex) {
    const int count = mutex->count;
    mutex->count = 1;
    OSUnlockMutex(mutex);
    wait(cond);
    OSLockMutex(mutex);
    mutex->count = count;
}
void OSSignalCond(OSCond* cond) { wake(cond); }

void OSInitMessageQueue(OSMessageQueue* queue, OSMessage* buffer, s32 count) {
    OSInitThreadQueue(&queue->queueSend);
    OSInitThreadQueue(&queue->queueReceive);
    queue->msgArray = buffer;
    queue->msgCount = count;
    queue->firstIndex = queue->usedCount = 0;
}
BOOL OSSendMessage(OSMessageQueue* queue, OSMessage message, s32 flags) {
    if (queue->msgCount <= 0) return FALSE;
    while (queue->usedCount == queue->msgCount) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        wait(&queue->queueSend);
    }
    queue->msgArray[(queue->firstIndex + queue->usedCount) % queue->msgCount] = message;
    ++queue->usedCount;
    wake(&queue->queueReceive);
    return TRUE;
}
BOOL OSReceiveMessage(OSMessageQueue* queue, OSMessage* message, s32 flags) {
    if (queue->msgCount <= 0) return FALSE;
    while (!queue->usedCount) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        wait(&queue->queueReceive);
    }
    if (message) *message = queue->msgArray[queue->firstIndex];
    queue->firstIndex = (queue->firstIndex + 1) % queue->msgCount;
    --queue->usedCount;
    wake(&queue->queueSend);
    return TRUE;
}
BOOL OSJamMessage(OSMessageQueue* queue, OSMessage message, s32 flags) {
    if (queue->msgCount <= 0) return FALSE;
    while (queue->usedCount == queue->msgCount) {
        if (flags == OS_MESSAGE_NOBLOCK) return FALSE;
        wait(&queue->queueSend);
    }
    queue->firstIndex = (queue->firstIndex + queue->msgCount - 1) % queue->msgCount;
    queue->msgArray[queue->firstIndex] = message;
    ++queue->usedCount;
    wake(&queue->queueReceive);
    return TRUE;
}
}
