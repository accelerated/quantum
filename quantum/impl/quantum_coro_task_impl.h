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
#include <quantum/quantum_allocator.h>
#include <quantum/quantum_traits.h>

namespace Bloomberg {
namespace quantum {

#ifndef __QUANTUM_USE_DEFAULT_ALLOCATOR
    #ifdef __QUANTUM_ALLOCATE_POOL_FROM_HEAP
        using TaskAllocator = HeapAllocator<CoroTask>;
    #else
        using TaskAllocator = StackAllocator<CoroTask, __QUANTUM_TASK_ALLOC_SIZE>;
    #endif
#else
    using TaskAllocator = StlAllocator<CoroTask>;
#endif

template <class RET, class FUNC, class ... ARGS>
CoroTask::CoroTask(std::false_type,
                   std::shared_ptr<Context<RET>> ctx,
                   int queueId,
                   bool isHighPriority,
                   Task::Type type,
                   FUNC&& func,
                   ARGS&&... args) :
    _coroContext(ctx),
    _coro(Allocator<CoroStackAllocator>::instance(AllocatorTraits::defaultCoroPoolAllocSize()),
          Util::bindCaller(ctx, std::forward<FUNC>(func), std::forward<ARGS>(args)...)),
    _queueId(queueId),
    _isHighPriority(isHighPriority),
    _type(type),
    _suspendedState((int)State::Suspended),
    _coroLocalStorage()
{}

template <class RET, class FUNC, class ... ARGS>
CoroTask::CoroTask(std::true_type,
                   std::shared_ptr<Context<RET>> ctx,
                   int queueId,
                   bool isHighPriority,
                   Task::Type type,
                   FUNC&& func,
                   ARGS&&... args) :
    _coroContext(ctx),
    _coro(Allocator<CoroStackAllocator>::instance(AllocatorTraits::defaultCoroPoolAllocSize()),
          Util::bindCaller2(ctx, std::forward<FUNC>(func), std::forward<ARGS>(args)...)),
    _queueId(queueId),
    _isHighPriority(isHighPriority),
    _type(type),
    _suspendedState((int)State::Suspended),
    _coroLocalStorage()
{}

inline
CoroTask::~CoroTask()
{
    terminate();
}

inline
void CoroTask::terminate()
{
    bool value{false};
    if (_terminated.compare_exchange_strong(value, true))
    {
        if (_coroContext) _coroContext->terminate();
    }
}

inline
int CoroTask::run()
{
    SuspensionGuard guard(_suspendedState);
    if (guard)
    {
        if (!_coro)
        {
            return (int)Task::Status::NotCallable;
        }
        if (isBlocked())
        {
            return (int)Task::Status::Blocked;
        }
        if (isSleeping(true))
        {
            return (int)Task::Status::Sleeping;
        }
        
        int rc = (int)Task::Status::Running;
        _coro(rc);
        if (!_coro)
        {
            guard.set((int)State::Terminated);
        }
        return rc;
    }
    return (int)Task::Status::AlreadyResumed;
}

inline
void CoroTask::setQueueId(int queueId)
{
    _queueId = queueId;
}

inline
int CoroTask::getQueueId()
{
    return _queueId;
}

inline
Task::Type CoroTask::getType() const { return _type; }

inline
CoroTaskPtr CoroTask::getNextTask() { return _next; }

inline
void CoroTask::setNextTask(CoroTaskPtr nextTask) { _next = nextTask; }

inline
CoroTaskPtr CoroTask::getPrevTask() { return _prev.lock(); }

inline
void CoroTask::setPrevTask(CoroTaskPtr prevTask) { _prev = prevTask; }

inline
CoroTaskPtr CoroTask::getFirstTask()
{
    return (_type == Task::Type::First) ? shared_from_this() : getPrevTask()->getFirstTask();
}

inline
CoroTaskPtr CoroTask::getErrorHandlerOrFinalTask()
{
    if ((_type == Task::Type::ErrorHandler) || (_type == Task::Type::Final))
    {
        return shared_from_this();
    }
    else if (_next)
    {
        CoroTaskPtr task = _next->getErrorHandlerOrFinalTask();
        if ((_next->getType() != Task::Type::ErrorHandler) && (_next->getType() != Task::Type::Final))
        {
            _next->terminate();
            _next.reset(); //release next task
        }
        return task;
    }
    return nullptr;
}

inline
bool CoroTask::isBlocked() const
{
    return _coroContext ? _coroContext->isBlocked() : false; //coroutine is waiting on some signal
}

inline
bool CoroTask::isSleeping(bool updateTimer)
{
    return _coroContext ? _coroContext->isSleeping(updateTimer) : false; //coroutine is sleeping
}

inline
bool CoroTask::isHighPriority() const
{
    return _isHighPriority;
}

inline
bool CoroTask::isSuspended() const
{
    return _suspendedState == (int)State::Suspended;
}

inline
CoroTask::CoroLocalStorage& CoroTask::getCoroLocalStorage()
{
    return _coroLocalStorage;
}

inline
ITaskAccessor::Ptr CoroTask::getTaskAccessor() const
{
    return _coroContext;
}

inline
void* CoroTask::operator new(size_t)
{
    return Allocator<TaskAllocator>::instance(AllocatorTraits::taskAllocSize()).allocate();
}

inline
void CoroTask::operator delete(void* p)
{
    Allocator<TaskAllocator>::instance(AllocatorTraits::taskAllocSize()).deallocate(static_cast<CoroTask*>(p));
}

inline
void CoroTask::deleter(CoroTask* p)
{
#ifndef __QUANTUM_USE_DEFAULT_ALLOCATOR
    Allocator<TaskAllocator>::instance(AllocatorTraits::taskAllocSize()).dispose(p);
#else
    delete p;
#endif
}
    
}}
