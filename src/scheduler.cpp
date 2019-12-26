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
#include "sched/scheduler.h"
#include "sched/platform.h"
#include "private.h"
#include <atomic>
#include <cstdlib>
#include <condition_variable>
#include <mutex>
#include <new>

using sched::Fiber;
using sched::Platform;
using sched::Scheduler;
using sched::Task;
using sched::TaskEntry;


struct sched::Task {
	Fiber* fiber = nullptr;
	Task* next = nullptr; ///NOTE(mendsley): Owned by TaskList

	TaskEntry* entry;
	void* entryParam;
};


namespace {
	struct SchedulerThread {
		Scheduler* scheduler;
		Task* currentTask = nullptr;
		Task initialTask;
	};


	struct TaskList {
		Task* front = nullptr;
		Task* last = nullptr;
	};
}


static thread_local SchedulerThread* s_tlsSchedulerThread;


struct sched::Scheduler {
	const Platform* platform; ///TODO(mendsley): Create a copy of the platform functions rather than storing a pointer

	std::mutex taskListMutex;
	std::condition_variable taskListCondvar;
	TaskList taskList;
	TaskList taskListCompleted;
	int activeThreads = 0;
};


template<typename T>
static T* construct(const Platform* platform)
{
	T* result = new(platform->allocate(sizeof(T))) T;
	assert_msg(result != nullptr, "Failed to allocate object");
	return result;
}

template<typename T>
static void destroy(const Platform* platform, T* ptr)
{
	ptr->~T();
	platform->free(ptr, sizeof(T));
}


static void taskListPush(TaskList* taskList, Task* task) {
	if (nullptr != taskList->last) {
		assert(taskList->last->next == nullptr);
		taskList->last->next = task;
	} else {
		taskList->front = task;
	}

	taskList->last = task;
	task->next = nullptr;
}


static Task* taskListPop(TaskList* taskList) {
	Task* task = taskList->front;
	if (nullptr != task) {
		taskList->front = task->next;
		task->next = nullptr;

		if (nullptr == taskList->front) {
			taskList->last = nullptr;
		}
	}

	return task;
}


// Single pass through the scheduler. Wait for a task and continue it
static void schedule_task(SchedulerThread* thread) {
	assert(thread);
	Scheduler* scheduler = thread->scheduler;
	assert(scheduler);
	const Platform* platform = scheduler->platform;
	assert(platform);
	Task* outgoingTask = thread->currentTask;
	assert(outgoingTask);
	Fiber* outgoingFiber = outgoingTask->fiber;
	assert(outgoingFiber);

	// find a task to run
	Task* task = nullptr;
	while (nullptr == task) {
		std::unique_lock<std::mutex> lock(scheduler->taskListMutex);
		if (0 == scheduler->activeThreads) {
			return;
		}

		task = taskListPop(&scheduler->taskList);
		if (nullptr == task) {
			scheduler->taskListCondvar.wait(lock);
		}
	}

	assert(task);
	thread->currentTask = task;
	platform->switchToFiber(outgoingFiber, task->fiber);

	// delete completed tasks
	scheduler->taskListMutex.lock();
	while (Task* task = taskListPop(&scheduler->taskListCompleted)) {
		assert(task);
		assert(task->fiber);

		platform->releaseFiber(task->fiber);
		destroy(platform, task);
	}
	scheduler->taskListMutex.unlock();
}


Scheduler* sched::createScheduler(const Platform* platform) {
	assert(platform);

	///TODO(mendsley): Validate/provide default platform

	Scheduler* scheduler = construct<Scheduler>(platform);
	scheduler->platform = platform;

	return scheduler;
}


void sched::destroyScheduler(Scheduler* scheduler) {
	assert(scheduler);

	if (0 != scheduler->activeThreads) {
		assert_msg(scheduler->activeThreads == 0, "Scheduler is still active on other threads");
		return;
	}

	const Platform* platform = scheduler->platform;
	destroy(platform, scheduler);
}


void sched::attachToThread(Scheduler* scheduler) {
	assert(scheduler);

	if (nullptr != s_tlsSchedulerThread) {
		assert_msg(s_tlsSchedulerThread == nullptr, "This thread is already attached to a scheduler");
		std::abort();
	}

	scheduler->taskListMutex.lock();
	++scheduler->activeThreads;
	scheduler->taskListMutex.unlock();

	const Platform* platform = scheduler->platform;
	SchedulerThread* thread = construct<SchedulerThread>(platform);
	thread->scheduler = scheduler;

	thread->initialTask.fiber = platform->createFiberForCurrentThread();
	assert(thread->initialTask.fiber);
	thread->currentTask = &thread->initialTask;

	s_tlsSchedulerThread = thread;
}


void sched::detachFromThread(Scheduler* scheduler) {
	SchedulerThread* thread = s_tlsSchedulerThread;
	assert(scheduler);
	if (nullptr != thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to this thread");
		return;
	} else if (thread->scheduler != scheduler) {
		assert_msg(thread->scheduler == scheduler, "This scheduler is not attached to the current thread");
		return;
	} else if (thread->currentTask != &thread->initialTask) {
		assert_msg(thread->currentTask == &thread->initialTask, "Not on the task that attached to the thread");
		return;
	}

	scheduler->taskListMutex.lock();
	int newActiveThreadCount = --scheduler->activeThreads;
	scheduler->taskListMutex.unlock();

	if (0 == newActiveThreadCount) {
		scheduler->taskListCondvar.notify_all();
	}

	const Platform* platform = scheduler->platform;
	platform->releaseFiberForCurrentThread(thread->initialTask.fiber);
	destroy(platform, thread);

	s_tlsSchedulerThread = nullptr;
}


void sched::waitForOtherThreadsAndDetach(Scheduler* scheduler) {
	SchedulerThread* thread = s_tlsSchedulerThread;
	assert(scheduler);
	if (nullptr == thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to the current thread");
		return;
	} else if (thread->scheduler != scheduler) {
		assert_msg(thread->scheduler == scheduler, "This scheduler is not attached to the current thread");
		return;
	}

	scheduler->taskListMutex.lock();
	--scheduler->activeThreads;
	while (scheduler->activeThreads > 0) {
		scheduler->taskListMutex.unlock();
		schedule_task(thread);
		scheduler->taskListMutex.lock();
	}
	scheduler->taskListMutex.unlock();

	const Platform* platform = scheduler->platform;
	platform->releaseFiberForCurrentThread(thread->initialTask.fiber);
	destroy(platform, thread);

	s_tlsSchedulerThread = nullptr;
}


Task* sched::spawn(TaskEntry* entry, void* param, int stackSize) {
	SchedulerThread* thread = s_tlsSchedulerThread;
	if (nullptr == thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to the current thread");
		std::abort();
	}

	assert(thread->scheduler);
	return spawn(thread->scheduler, entry, param, stackSize);
}


static void taskFiber(void* context) {
	Task* task = (Task*)context;
	assert(task);
	assert(task->entry);

	task->entry(task->entryParam);

	SchedulerThread* thread = s_tlsSchedulerThread;
	assert(thread);
	Scheduler* scheduler = thread->scheduler;
	assert(scheduler);

	scheduler->taskListMutex.lock();
	taskListPush(&scheduler->taskListCompleted, task);
	scheduler->taskListMutex.unlock();

	schedule_task(thread);
}


Task* sched::spawn(Scheduler* scheduler, TaskEntry* entry, void* param, int stackSize) {
	assert(scheduler);
	assert(entry);

	if (0 >= stackSize) {
		stackSize = 1024*1024; ///TODO(mendsley): Should we allow the deafault stack size to be configured?
	}

	const Platform* platform = scheduler->platform;
	Task* task = construct<Task>(platform);
	task->entry = entry;
	task->entryParam = param;
	task->fiber = platform->createFiber(taskFiber, task, stackSize);
	assert(task->fiber);

	scheduler->taskListMutex.lock();
	taskListPush(&scheduler->taskList, task);
	scheduler->taskListMutex.unlock();
	scheduler->taskListCondvar.notify_one();

	return task;
}


Task* sched::getCurrentTask() {
	SchedulerThread* thread = s_tlsSchedulerThread;
	if (nullptr == thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to the current thread");
		std::abort();
	}

	assert(thread->currentTask);
	return thread->currentTask;
}


void sched::yield() {
	Task* task = getCurrentTask();
	assert(task);

	wake(task);
	suspendSelf();
}


void sched::suspendSelf() {
	SchedulerThread* thread = s_tlsSchedulerThread;
	if (nullptr == thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to the current thread");
		return;
	}

	Scheduler* scheduler = thread->scheduler;
	assert(scheduler);

	schedule_task(thread);
}


void sched::wake(Task* task) {
	SchedulerThread* thread = s_tlsSchedulerThread;
	if (nullptr == thread) {
		assert_msg(thread != nullptr, "No scheduler is attached to the current thread");
	} else if (nullptr == task) {
		assert(task != nullptr);
		return;
	}

	Scheduler* scheduler = thread->scheduler;
	assert(scheduler);

	scheduler->taskListMutex.lock();
	taskListPush(&scheduler->taskList, task);
	scheduler->taskListMutex.unlock();
	scheduler->taskListCondvar.notify_one();
}
