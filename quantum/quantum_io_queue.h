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
#ifndef BLOOMBERG_QUANTUM_IO_QUEUE_H
#define BLOOMBERG_QUANTUM_IO_QUEUE_H

#include <list>
#include <thread>
#include <condition_variable>
#include <iostream>
#include <atomic>
#include <quantum/quantum_task.h>
#include <quantum/interface/quantum_iterminate.h>
#include <quantum/quantum_queue.h>
#include <quantum/quantum_io_task.h>
#include <quantum/quantum_queue_statistics.h>
#include <quantum/quantum_configuration.h>

namespace Bloomberg {
namespace quantum {

//==============================================================================================
//                                 class IoQueue
//==============================================================================================
/// @class IoQueue
/// @brief Thread queue for executing IO tasks.
/// @note For internal use only.
class IoQueue : public ITerminate
{
public:
    using TaskList = std::list<IoTaskPtr, IoQueueListAllocator>;
    using TaskListIter = TaskList::iterator;
    
    IoQueue();
    
    IoQueue(const Configuration& config,
            std::vector<IoQueue>* sharedIoQueues);
    
    IoQueue(const IoQueue& other);
    
    IoQueue(IoQueue&& other) = default;
    
    ~IoQueue();
    
    void terminate() final;
    
    void pinToCore(int coreId);
    
    void run();
    
    void enqueue(IoTaskPtr task);
    
    bool tryEnqueue(IoTaskPtr task);
    
    IoTaskPtr dequeue(std::atomic_bool& hint);
    
    IoTaskPtr tryDequeue(std::atomic_bool& hint);
    
    size_t size() const;
    
    bool empty() const;
    
    IQueueStatistics& stats();
    
    SpinLock& getLock();
    
    void signalEmptyCondition(bool value);
    
    bool isIdle() const;
    
    const std::shared_ptr<std::thread>& getThread() const;
    
private:
    IoTaskPtr grabWorkItem();
    IoTaskPtr grabWorkItemFromAll();
    void doEnqueue(IoTaskPtr task);
    IoTaskPtr doDequeue(std::atomic_bool& hint);
    IoTaskPtr tryDequeueFromShared();
    std::chrono::milliseconds getBackoffInterval();
    
    //async IO queue
    std::vector<IoQueue>*           _sharedIoQueues;
    bool                            _loadBalanceSharedIoQueues;
    std::chrono::milliseconds       _loadBalancePollIntervalMs;
    Configuration::BackoffPolicy    _loadBalancePollIntervalBackoffPolicy;
    size_t                          _loadBalancePollIntervalNumBackoffs;
    size_t                          _loadBalanceBackoffNum;
    std::shared_ptr<std::thread>    _thread;
    TaskList                        _queue;
    mutable SpinLock                _spinlock;
    std::mutex                      _notEmptyMutex; //for accessing the condition variable
    std::condition_variable         _notEmptyCond;
    std::atomic_bool                _isEmpty;
    std::atomic_bool                _isInterrupted;
    std::atomic_bool                _isIdle;
    std::atomic_bool                _terminated;
    QueueStatistics                 _stats;
};

}}

#include <quantum/impl/quantum_io_queue_impl.h>

#endif //BLOOMBERG_QUANTUM_IO_QUEUE_H
