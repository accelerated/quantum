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
#ifndef BLOOMBERG_QUANTUM_CORO_TASK_H
#define BLOOMBERG_QUANTUM_CORO_TASK_H

#include <iostream>
#include <memory>
#include <list>
#include <utility>
#include <unordered_map>
#include <quantum/interface/quantum_iterminate.h>
#include <quantum/interface/quantum_icontext.h>
#include <quantum/interface/quantum_itask_accessor.h>
#include <quantum/quantum_traits.h>
#include <quantum/util/quantum_util.h>

namespace Bloomberg {
namespace quantum {

//==============================================================================================
//                                 class CoroTask
//==============================================================================================
/// @class CoroTask.
/// @brief Runnable object representing a coroutine.
/// @note For internal use only.
class CoroTask : public ITerminate,
                 public std::enable_shared_from_this<CoroTask>
{
public:
    using Ptr = std::shared_ptr<CoroTask>;
    using WeakPtr = std::weak_ptr<CoroTask>;
    
    enum class State : int { Running, Suspended, Terminated };

    using CoroLocalStorage = std::unordered_map<std::string, void*>;
    
    template <class RET, class FUNC, class ... ARGS>
    CoroTask(std::false_type t,
             std::shared_ptr<Context<RET>> ctx,
             int queueId,
             bool isHighPriority,
             Task::Type type,
             FUNC&& func,
             ARGS&&... args);
    
    template <class RET, class FUNC, class ... ARGS>
    CoroTask(std::true_type t,
             std::shared_ptr<Context<RET>> ctx,
             int queueId,
             bool isHighPriority,
             Task::Type type,
             FUNC&& func,
             ARGS&&... args);
    
    CoroTask(const CoroTask& task) = delete;
    CoroTask(CoroTask&& task) = default;
    CoroTask& operator=(const CoroTask& task) = delete;
    CoroTask& operator=(CoroTask&& task) = default;
    
    ~CoroTask();
    
    //ITerminate
    void terminate() final;
    
    int run();
    void setQueueId(int queueId);
    int getQueueId();
    Task::Type getType() const;
    bool isBlocked() const;
    bool isSleeping(bool updateTimer = false);
    bool isHighPriority() const;
    bool isSuspended() const;
    
    Ptr getNextTask();
    void setNextTask(Ptr nextTask);
    Ptr getPrevTask();
    void setPrevTask(Ptr prevTask);
    Ptr getFirstTask();
    
    //Returns a final or error handler task in the chain and in the process frees all
    //the subsequent continuation tasks
    Ptr getErrorHandlerOrFinalTask();

    //Local storage accessors
    CoroLocalStorage& getCoroLocalStorage();
    ITaskAccessor::Ptr getTaskAccessor() const;

    //===================================
    //           NEW / DELETE
    //===================================
    static void* operator new(size_t size);
    static void operator delete(void* p);
    static void deleter(CoroTask* p);
    
private:
    struct SuspensionGuard {
        SuspensionGuard(std::atomic_int& suspendedState) :
            _isLocked(false),
            _suspendedState(suspendedState)
        {
            int suspended = (int)State::Suspended;
            _isLocked = _suspendedState.compare_exchange_strong(suspended,
                                                                (int)State::Running,
                                                                std::memory_order::memory_order_acq_rel);
        }
        ~SuspensionGuard()
        {
            if (_isLocked)
            {
                _suspendedState.store((int)State::Suspended, std::memory_order::memory_order_acq_rel);
            }
        }
        void set(int newState)
        {
            _suspendedState.store(newState, std::memory_order::memory_order_acq_rel);
            _isLocked = false;
        }
               
        operator bool() const
        {
            return _isLocked;
        }
        
        bool _isLocked;
        std::atomic_int& _suspendedState;
    };
    
    ITaskAccessor::Ptr          _coroContext; //holds execution context
    Traits::Coroutine           _coro; //the current runnable coroutine
    int                         _queueId;
    bool                        _isHighPriority;
    Ptr                         _next; //CoroTask scheduled to run after current completes.
    WeakPtr                     _prev; //Previous task in the chain
    Task::Type                  _type;
    std::atomic_int             _suspendedState; // stores values of State
    CoroLocalStorage            _coroLocalStorage; // local storage of the coroutine
};

using CoroTaskPtr = CoroTask::Ptr;
using CoroTaskWeakPtr = CoroTask::WeakPtr;

}}

#include <quantum/impl/quantum_coro_task_impl.h>

#endif //BLOOMBERG_QUANTUM_CORO_TASK_H
