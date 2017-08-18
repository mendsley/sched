/**
* Copyright 2015-2017 Matthew Endsley
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

#include <functional>
#include "sched/config.h"

namespace sched {

	struct FiberFactory;
	struct Scheduler;
	struct Task;

	// Controlls lifetime of a scheduler thread
	struct SCHED_NO_VTABLE RunContext
	{
		virtual bool running() const = 0;
	};

	Scheduler* createScheduler(FiberFactory* factory);
	void destroyScheduler(Scheduler* scheduler);

	// get the fiber factory for a scheduler
	FiberFactory* getFiberFactory(Scheduler* scheduler);

	// runs the scheduler on this thread
	void run(Scheduler* scheduler, const RunContext* runContext);

	// create a scheduler on this thread until entry returns
	void runFunction(FiberFactory* factory, int nthreads, std::function<void(sched::Scheduler* scheduler)> entry);

	// Create a new task on the specified scheduler
	Task* spawn(Scheduler* scheduler, std::function<void()> entry, int stackSize = 0);

	// Create a new task on the current task's scheduler
	Task* spawn(std::function<void()> entry, int stackSize = 0);

	// Gets the currently executing task
	Task* currentTask();

	// Yeild control back to the scheduler. The current task will be
	// automatically rescheduler to continue. Blocks until the current task
	// is scheduled
	void yield();

	// Suspends the task. This blocks until a corresponding call to
	// wake(t) is made.
	void suspendSelf();

	// Wakes the specified task. Unblocks a call to suspendSelf, and causes the
	// call to suspendSelf to return.
	void wake(Task* t);

} // namespace sched
