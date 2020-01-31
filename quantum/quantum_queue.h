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
#ifndef BLOOMBERG_QUANTUM_QUEUE_H
#define BLOOMBERG_QUANTUM_QUEUE_H

#include <quantum/quantum_spinlock.h>
#include <quantum/quantum_task.h>
#include <quantum/interface/quantum_iqueue_statistics.h>
#include <quantum/quantum_allocator.h>
#ifndef __GLIBC__
#include <pthread.h>
#endif
#include <thread>
#include <string.h>

namespace Bloomberg {
namespace quantum {

//==============================================================================================
//                                  struct Queue
//==============================================================================================
/// @struct Queue
/// @brief Interface to a task queue. For internal use only.
struct Queue
{
    //Typedefs and enum definitions
    enum class Type : int { Coro, IO, All };
    enum class Id : int { Any = -1, Same = -2, All = -3 };
    
    //Backward compatibility name forwarding
    using QueueId = Id;
    using QueueType = Type;
    
    static void setThreadName(Type type,
                              std::thread::native_handle_type threadHandle,
                              int queueId,
                              bool shared,
                              bool any);
};

//Backward compatibility name forwarding
using IQueue = Queue;

inline
void Queue::setThreadName(Queue::Type type,
                          std::thread::native_handle_type threadHandle,
                          int queueId,
                          bool shared,
                          bool any)
{
    int idx = 0;
    char name[16] = {0};
    memcpy(name + idx, "quantum:", 8); idx += 8;
    if (type == Queue::Type::Coro) {
        memcpy(name + idx, "co:", 3); idx += 3;
        if (shared) {
            memcpy(name + idx, "s:", 2); idx += 2;
        }
        else if (any) {
            memcpy(name + idx, "a:", 2); idx += 2;
        }
    }
    else {
        memcpy(name + idx, "io:", 3); idx += 3;
    }
    //last 2 digits of the queueId
    name[idx+1] = '0' + queueId % 10; queueId /= 10;
    name[idx] = '0' + queueId % 10;
    
    #if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 12)
        pthread_setname_np(threadHandle, name);
    #endif
}

#ifndef __QUANTUM_USE_DEFAULT_ALLOCATOR
    #ifdef __QUANTUM_ALLOCATE_POOL_FROM_HEAP
        using QueueListAllocator = HeapAllocator<CoroTaskPtr>;
        using IoQueueListAllocator = HeapAllocator<IoTaskPtr>;
    #else
        using QueueListAllocator = StackAllocator<CoroTaskPtr, __QUANTUM_QUEUE_LIST_ALLOC_SIZE>;
        using IoQueueListAllocator = StackAllocator<IoTaskPtr, __QUANTUM_IO_QUEUE_LIST_ALLOC_SIZE>;
    #endif
#else
    using QueueListAllocator = StlAllocator<ITask::Ptr>;
    using IoQueueListAllocator = StlAllocator<ITask::Ptr>;
#endif

}}

#endif //BLOOMBERG_QUANTUM_QUEUE_H
