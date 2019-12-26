/**
* Copyright 2015-2019 Matthew Endsley
* All rights reserved
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted providing that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once

#include <sched/platform.h>

namespace sched {

	struct Scheduler;
	struct Task;

	using TaskEntry = void(void* param);

	// Create a new scheduling context
	Scheduler* createScheduler(const Platform* platform);

	// Destroy a scheduling context
	void destroyScheduler(Scheduler* scheduler);

	// Attach the scheduler to the current thread
	void attachToThread(Scheduler* scheduler);

	// Detach the scheduler from the current thread
	void detachFromThread(Scheduler* scheduler);

	// Schedule tasks until all attached threads have either called
	// `detachFromThread` or `waitForOtherThreadsAndDetach`
	void waitForOtherThreadsAndDetach(Scheduler* scheduler);

	// Creates a new task on the current thread's scheduler
	Task* spawn(TaskEntry* entry, void* param, int stackSize = 0);

	// Creates a new task on a specific scheduler
	Task* spawn(Scheduler* scheduler, TaskEntry* entry, void* param, int stackSize = 0);

	// Gets the currently executing task
	Task* getCurrentTask();

	// Yield control back to the scheduler. The current task will be
	// automatically continued. This function blocks until the task
	// is continued.
	void yield();

	// Suspend the task. This blocks until `wake(t)` is called for
	// the currently running task
	void suspendSelf();

	// Wakes the specified task. This unblocks a call to suspendSelf
	void wake(Task* t);
}
