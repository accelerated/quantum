/*
** Copyright 2018 Bloomberg Finance L.P.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
#ifndef BLOOMBERG_QUANTUM_CORO_QUEUE_H
#define BLOOMBERG_QUANTUM_CORO_QUEUE_H

#include <list>
#include <atomic>
#include <functional>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <pthread.h>
#include <iostream>
#include <quantum/quantum_maybe.h>
#include <quantum/quantum_queue.h>
#include <quantum/interface/quantum_iterminate.h>
#include <quantum/quantum_spinlock.h>
#include <quantum/quantum_coro_task.h>
#include <quantum/quantum_yielding_thread.h>
#include <quantum/quantum_queue_statistics.h>
#include <quantum/quantum_configuration.h>

namespace Bloomberg {
namespace quantum {

//==============================================================================================
//                                 class CoroQueue
//==============================================================================================
/// @class CoroQueue.
/// @brief Thread queue for running coroutines.
/// @note For internal use only.
class CoroQueue : public ITerminate
{
public:
    using TaskList = std::list<CoroTask, ContiguousPoolManager<CoroTask>>;
    using TaskListIter = TaskList::iterator;
    
    CoroQueue();
    
    CoroQueue(const Configuration& config,
              std::shared_ptr<CoroQueue> sharedQueue);
    
    CoroQueue(const CoroQueue& other);
    
    CoroQueue(CoroQueue&& other) = default;
    
    ~CoroQueue();
    
    void pinToCore(int coreId);
    
    void run();
    
    void enqueue(CoroTask&& task);
    
    bool tryEnqueue(CoroTask&& task);
    
    void dequeue(std::atomic_bool& hint);
    
    void tryDequeue(std::atomic_bool& hint);
    
    size_t size() const;
    
    bool empty() const;
    
    void terminate() final;
    
    IQueueStatistics& stats();
    
    SpinLock& getLock();
    
    void signalEmptyCondition(bool value);
    
    bool isIdle() const;
    
    const std::shared_ptr<std::thread>& getThread() const;

    // Coroutine local API
    static CoroTask* getCurrentTask();

    static void setCurrentTask(CoroTask* task);

private:
    struct WorkItem
    {
        WorkItem(CoroTask* task,
                 TaskListIter iter,
                 bool isBlocked,
                 unsigned int blockedQueueRound);
        
        CoroTask*       _task;              // task pointer
        TaskListIter    _iter;              // task iterator
        bool            _isBlocked;         // true if the entire queue is blocked
        unsigned int    _blockedQueueRound; // blocked queue round id
    };
    struct ProcessTaskResult
    {
        ProcessTaskResult(bool isBlocked,
                          unsigned int blockedQueueRound);
        bool            _isBlocked;         // true if the entire queue is blocked
        unsigned int    _blockedQueueRound; // blocked queue round id
    };
    struct CurrentTaskSetter
    {
        CurrentTaskSetter(CoroQueue& taskQueue, CoroTask* task);
        ~CurrentTaskSetter();

        CoroQueue& _taskQueue;
    };
    //Coroutine result handlers
    bool handleNotCallable(const WorkItem& entry);
    bool handleAlreadyResumed(WorkItem& entry);
    bool handleRunning(WorkItem& entry);
    bool handleSuccess(const WorkItem& entry);
    bool handleBlocked(WorkItem& entry);
    bool handleSleeping(WorkItem& entry);
    bool handleError(const WorkItem& entry);
    bool handleException(const WorkItem& workItem,
                         const std::exception* ex = nullptr);

    void onBlockedTask(WorkItem& entry);
    void onActiveTask(WorkItem& entry);
    
    bool isInterrupted();
    void signalSharedQueueEmptyCondition(bool value);
    ProcessTaskResult processTask();
    WorkItem grabWorkItem();
    void doEnqueue(CoroTask&& task);
    void doDequeue(std::atomic_bool& hint,
                   TaskListIter iter);
    void acquireWaiting();
    void sleepOnBlockedQueue(const ProcessTaskResult& mainQueueResult);
    void sleepOnBlockedQueue(const ProcessTaskResult& mainQueueResult,
                             const ProcessTaskResult& sharedQueueResult);

    
    QueueListAllocator                  _alloc;
    std::shared_ptr<std::thread>        _thread;
    TaskList                            _runQueue;
    TaskList                            _waitQueue;
    TaskListIter                        _queueIt;
    TaskListIter                        _blockedIt;
    bool                                _isBlocked;
    mutable SpinLock                    _runQueueLock;
    mutable SpinLock                    _waitQueueLock;
    std::mutex                          _notEmptyMutex; //for accessing the condition variable
    std::condition_variable             _notEmptyCond;
    std::atomic_bool                    _isEmpty;
    std::atomic_bool                    _isSharedQueueEmpty;
    std::atomic_bool                    _isInterrupted;
    std::atomic_bool                    _isIdle;
    bool                                _isAdvanced;
    QueueStatistics                     _stats;
    std::shared_ptr<CoroQueue>          _sharedQueue;
    std::vector<CoroQueue*>             _helpers;
    unsigned int                        _queueRound;
    unsigned int                        _lastSleptQueueRound;
    unsigned int                        _lastSleptSharedQueueRound;
};

}}

#include <quantum/impl/quantum_coro_queue_impl.h>

#endif //BLOOMBERG_QUANTUM_CORO_QUEUE_H
