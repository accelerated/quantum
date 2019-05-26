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
//NOTE: DO NOT INCLUDE DIRECTLY

//##############################################################################################
//#################################### IMPLEMENTATIONS #########################################
//##############################################################################################

namespace Bloomberg {
namespace quantum {
    
inline
TaskQueue::TaskQueue() :
    TaskQueue(Configuration(), nullptr)
{
}

inline
TaskQueue::TaskQueue(const Configuration&, std::shared_ptr<TaskQueue> sharedQueue) :
    _alloc(Allocator<QueueListAllocator>::instance(AllocatorTraits::queueListAllocSize())),
    _runQueue(_alloc),
    _waitQueue(_alloc),
    _queueIt(_runQueue.end()),
    _blockedIt(_runQueue.end()),
    _isEmpty(true),
    _isSharedQueueEmpty(true),
    _isInterrupted(false),
    _isIdle(true),
    _terminated(false),
    _isAdvanced(false),
    _sharedQueue(sharedQueue)
{
    if (_sharedQueue)
    {
        _helpers.push_back(this);
    }
    _thread = std::make_shared<std::thread>(std::bind(&TaskQueue::run, this));
}

inline
TaskQueue::TaskQueue(const TaskQueue&) :
    TaskQueue()
{

}

inline
TaskQueue::~TaskQueue()
{
    terminate();
}

inline
void TaskQueue::pinToCore(int coreId)
{
#ifdef _WIN32
    SetThreadAffinityMask(_thread->native_handle(), 1 << coreId);
#else
    int cpuSetSize = sizeof(cpu_set_t);
    if (coreId >= 0 && (coreId <= cpuSetSize*8))
    {
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(coreId, &cpuSet);
        pthread_setaffinity_np(_thread->native_handle(), cpuSetSize, &cpuSet);
    }
#endif
}


inline
void TaskQueue::run()
{
    while (!isInterrupted())
    {
        processTask();
        if (_sharedQueue)
        {
            _sharedQueue->processTask();
        }
    }
}

inline
bool TaskQueue::processTask()
{
    WorkItem workItem{nullptr, _runQueue.end()};
    try
    {
        //Process a task
        workItem = grabWorkItem();
        TaskPtr task = workItem.first;
        if (!task)
        {
            return false;
        }
        //========================= START/RESUME COROUTINE =========================
        int rc = task->run();
        //=========================== END/YIELD COROUTINE ==========================
        switch (rc)
        {
            case (int)ITask::RetCode::NotCallable:
                return handleNotCallable(workItem);
            case (int)ITask::RetCode::AlreadyResumed:
                return handleAlreadyResumed(workItem);
            case (int)ITask::RetCode::Running:
                return handleRunning(workItem);
            case (int)ITask::RetCode::Success:
                return handleSuccess(workItem);
            default:
                return handleError(workItem);
        }
    }
    catch (const std::exception& ex)
    {
        return handleException(workItem, &ex);
    }
    catch (...)
    {
        return handleException(workItem);
    }
}

inline
void TaskQueue::enqueue(ITask::Ptr task)
{
    if (!task)
    {
        return; //nothing to do
    }
    //========================= LOCKED SCOPE =========================
    SpinLock::Guard lock(_waitQueueLock);
    doEnqueue(task);
}

inline
bool TaskQueue::tryEnqueue(ITask::Ptr task)
{
    if (!task)
    {
        return false; //nothing to do
    }
    //========================= LOCKED SCOPE =========================
    SpinLock::Guard lock(_waitQueueLock, SpinLock::TryToLock{});
    if (lock.ownsLock())
    {
        doEnqueue(task);
    }
    return lock.ownsLock();
}

inline
void TaskQueue::doEnqueue(ITask::Ptr task)
{
    //NOTE: _queueIt remains unchanged following this operation
    bool isEmpty = _waitQueue.empty();
    if (task->isHighPriority())
    {
        //insert before the current position. If _queueIt == begin(), then the new
        //task will be at the head of the queue.
        _waitQueue.emplace_front(std::static_pointer_cast<Task>(task));
    }
    else
    {
        //insert after the current position. If next(_queueIt) == end()
        //then the new task will be the last element in the queue
        _waitQueue.emplace_back(std::static_pointer_cast<Task>(task));
    }
    if (task->isHighPriority())
    {
        _stats.incHighPriorityCount();
    }
    _stats.incPostedCount();
    _stats.incNumElements();
    if (isEmpty)
    {
        //signal on transition from 0 to 1 element only
        signalEmptyCondition(false);
    }
}

inline
ITask::Ptr TaskQueue::dequeue(std::atomic_bool& hint)
{
    return doDequeue(hint, _queueIt);
}

inline
ITask::Ptr TaskQueue::tryDequeue(std::atomic_bool& hint)
{
    return doDequeue(hint, _queueIt);
}

inline
ITask::Ptr TaskQueue::doDequeue(std::atomic_bool& hint, TaskListIter iter)
{
    //========================= LOCKED SCOPE =========================
    ReadWriteSpinLock::WriteGuard lock(_rwLock);
    hint = (iter == _runQueue.end());
    if (hint)
    {
        return nullptr;
    }
    ITask::Ptr task = *iter;
    task->terminate();
    if (_queueIt == iter)
    {
        _queueIt = _runQueue.erase(iter);
        _isAdvanced = true;
    }
    else
    {
        _runQueue.erase(iter);
    }
    _stats.decNumElements();
    return task;
}

inline
size_t TaskQueue::size() const
{
    return _stats.numElements();
}

inline
bool TaskQueue::empty() const
{
    return _stats.numElements() == 0;
}

inline
void TaskQueue::terminate()
{
    bool value{false};
    if (_terminated.compare_exchange_strong(value, true))
    {
        {
            std::unique_lock<std::mutex> lock(_notEmptyMutex);
            _isInterrupted = true;
        }
        _notEmptyCond.notify_all();
        _thread->join();
        
        //clear the queues
        while (!_runQueue.empty())
        {
            _runQueue.front()->terminate();
            _runQueue.pop_front();
        }
        //========================= LOCKED SCOPE =========================
        SpinLock::Guard lock(_waitQueueLock);
        while (!_waitQueue.empty())
        {
            _waitQueue.front()->terminate();
            _waitQueue.pop_front();
        }
    }
}

inline
IQueueStatistics& TaskQueue::stats()
{
    return _stats;
}

inline
SpinLock& TaskQueue::getLock()
{
    return _waitQueueLock;
}

inline
void TaskQueue::signalEmptyCondition(bool value)
{
    {
        //========================= LOCKED SCOPE =========================
        std::lock_guard<std::mutex> lock(_notEmptyMutex);
        _isEmpty = value;
    }
    if (!value)
    {
        _notEmptyCond.notify_all();
    }
    // Notify helpers as well
    for(TaskQueue* helper : _helpers)
    {
        helper->signalSharedQueueEmptyCondition(value);
    }
}

inline
void TaskQueue::signalSharedQueueEmptyCondition(bool value)
{
    {
        //========================= LOCKED SCOPE =========================
        std::lock_guard<std::mutex> lock(_notEmptyMutex);
        _isSharedQueueEmpty = value;
    }
    if (!value)
    {
        _notEmptyCond.notify_all();
    }
}

inline
bool TaskQueue::handleNotCallable(const WorkItem& workItem)
{
    return handleError(workItem);
}

inline
bool TaskQueue::handleAlreadyResumed(const WorkItem&)
{
    // we cannot run this coroutine since it's executing on another thread
    return false;
}

inline
bool TaskQueue::handleRunning(const WorkItem&)
{
    return true;
}

inline
bool TaskQueue::handleSuccess(const WorkItem& workItem)
{
    ITaskContinuation::Ptr nextTask;
    //Coroutine ended normally with "return 0" statement
    _stats.incCompletedCount();
    
    //check if there's another task scheduled to run after this one
    nextTask = workItem.first->getNextTask();
    if (nextTask && (nextTask->getType() == ITask::Type::ErrorHandler))
    {
        //skip error handler since we don't have any errors
        nextTask->terminate(); //invalidate the error handler
        nextTask = nextTask->getNextTask();
    }
    //queue next task and de-queue current one
    enqueue(nextTask);
    doDequeue(_isIdle, workItem.second);
    return true;
}

inline
bool TaskQueue::handleError(const WorkItem& workItem)
{
    ITaskContinuation::Ptr nextTask;
    //Coroutine ended with explicit user error
    _stats.incErrorCount();
    //Check if we have a final task to run
    nextTask = workItem.first->getErrorHandlerOrFinalTask();
    //queue next task and de-queue current one
    enqueue(nextTask);
    doDequeue(_isIdle, workItem.second);
#ifdef __QUANTUM_PRINT_DEBUG
    std::lock_guard<std::mutex> guard(Util::LogMutex());
    if (rc == (int)ITask::RetCode::Exception)
    {
        std::cerr << "Coroutine exited with user exception." << std::endl;
    }
    else
    {
        std::cerr << "Coroutine exited with error : " << rc << std::endl;
    }
#endif
    return false;
}

inline
bool TaskQueue::handleException(const WorkItem& workItem, const std::exception* ex)
{
    UNUSED(ex);
    doDequeue(_isIdle, workItem.second);
#ifdef __QUANTUM_PRINT_DEBUG
    std::lock_guard<std::mutex> guard(Util::LogMutex());
    if (ex != nullptr) {
        std::cerr << "Caught exception: " << ex.what() << std::endl;
    }
    else {
        std::cerr << "Caught unknown exception." << std::endl;
    }
#endif
    return false;
}

inline
bool TaskQueue::isInterrupted()
{
    if (_isEmpty && _isSharedQueueEmpty)
    {
        std::unique_lock<std::mutex> lock(_notEmptyMutex);
        //========================= BLOCK WHEN EMPTY =========================
        //Wait for the queue to have at least one element
        _notEmptyCond.wait(lock, [this]()->bool { return !_isEmpty || !_isSharedQueueEmpty || _isInterrupted; });
    }
    return _isInterrupted;
}

inline
std::pair<TaskPtr, TaskQueue::TaskListIter>
TaskQueue::grabWorkItem()
{
    auto it = _runQueue.end();
    {//========================= LOCKED SCOPE =========================
        ReadWriteSpinLock::WriteGuard guard(_rwLock);
        if ((_queueIt == _runQueue.end()) || (!_isAdvanced && (++_queueIt == _runQueue.end())))
        {
            acquireWaiting();
        }
        _isAdvanced = false; //reset flag
        if (_runQueue.empty())
        {
            return {nullptr, _runQueue.end()};
        }
        it = _queueIt;
    }
    Task::Ptr task = *it;
    if (task->isBlocked() || task->isSleeping(true) || !task->isSuspended())
    {
        return {nullptr, _runQueue.end()};
    }
    return {task, it};
}

inline
bool TaskQueue::isIdle() const
{
    return _isIdle;
}

inline
void TaskQueue::acquireWaiting()
{
    //========================= LOCKED SCOPE =========================
    SpinLock::Guard lock(_waitQueueLock);
    bool isEmpty = _runQueue.empty();
    if (_waitQueue.empty())
    {
        if (isEmpty)
        {
            signalEmptyCondition(true);
        }
        _queueIt = _runQueue.begin();
        return;
    }
    if (!isEmpty)
    {
        //rewind by one since we are at end()
        --_queueIt;
    }
    {
        //splice wait queue unto run queue.
        _runQueue.splice(_runQueue.end(), _waitQueue);
    }
    if (!isEmpty)
    {
        //move to first element from spliced queue
        ++_queueIt;
    }
    else
    {
        _queueIt = _runQueue.begin();
    }
}

}}

