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
#ifndef BLOOMBERG_QUANTUM_TASK_H
#define BLOOMBERG_QUANTUM_TASK_H

#include <memory>

namespace Bloomberg {
namespace quantum {

//==============================================================================================
//                                   Task
//==============================================================================================
/// @struct Task
struct Task
{
    enum class Type : int
    {
        Standalone, First, Continuation, ErrorHandler, Final, Termination, IO
    };
    
    enum class Status : int
    {
        Success = 0,                                ///< Coroutine finished successfully
        Running = std::numeric_limits<int>::max(),  ///< Coroutine is still active
        AlreadyResumed = (int)Running-1,            ///< Coroutine is running on a different thread
        Exception = (int)Running-2,                 ///< Coroutine ended in an exception
        NotCallable = (int)Running-3,               ///< Coroutine cannot be called
        Blocked = (int)Running-4,                   ///< Coroutine is blocked
        Sleeping = (int)Running-5,                  ///< Coroutine is sleeping
        Max = (int)Running-10,                      ///< Value of the max reserved return code
    };
};

//Name forwarding
class CoroTask;
using CoroTaskPtr = std::shared_ptr<CoroTask>;
class IoTask;
using IoTaskPtr = std::shared_ptr<IoTask>;

}}

#endif //BLOOMBERG_QUANTUM_TASK_H
