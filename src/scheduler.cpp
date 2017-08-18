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

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "private.h"
#include "sched/fiber.h"
#include "sched/scheduler.h"

using namespace sched;

namespace {
	struct TaskList
	{
		Task* front = nullptr;
		Task* last = nullptr;
	};

	// thread specific scheduler context
	struct SchedulerThread
	{
		Scheduler* scheduler;
		Fiber* fiber;
		Task* current;
		bool deleteLastFiber;
	};
} // namespace `anonymous'

// task context
struct sched::Task
{
	SchedulerThread* thread; // owned by Scheduler
	Fiber* fiber;
	Task* next; // owned and initialized by TaskList

	void (*unlock)(void* context) = nullptr;
	void* unlockContext = nullptr;

	std::mutex runLock;
};

// scheduler data shared among all threads
struct sched::Scheduler
{
	std::mutex runlistLock;
	std::condition_variable runlistCond;
	TaskList runlist;

	FiberFactory* factory;
};

static std::once_flag g_timersRunning;
static TimerContext g_timers;
static thread_local SchedulerThread* g_currentThreadScheduler;

static bool tasklistEmpty(const TaskList* tl)
{
	return tl->front == nullptr;
}

static Task* tasklistPop(TaskList* tl)
{
	Task* t = tl->front;
	if (t)
	{
		tl->front = t->next;
		if (!tl->front)
		{
			tl->last = nullptr;
		}
	}
	return t;
}

static void tasklistPush(TaskList* tl, Task* t)
{
	if (tl->last)
	{
		tl->last->next = t;
		tl->last = t;
	}
	else
	{
		tl->front = tl->last = t;
	}

	t->next = nullptr;
}

// create a new task, but do not schedule it
static Task* createTask(FiberFactory* factory, Fiber* current, std::function<void()> entry, int stackSize)
{
	struct Context
	{
		Task* result = nullptr;
		FiberFactory* factory;
		Fiber* callingFiber;
		std::function<void()> entry;
	};

	Context ctx;
	ctx.factory = factory;
	ctx.callingFiber = current;
	ctx.entry = std::move(entry);

	Fiber* fiber = factory->create([](Fiber* self, void* context) -> Fiber* {
		Context* ctx = static_cast<Context*>(context);

		Task task;
		task.fiber = self;
		std::function<void()> taskEntry = std::move(ctx->entry);

		// return controller back to createTask
		ctx->result = &task;
		ctx->factory->switchTo(self, ctx->callingFiber);

		// NOTE: ctx has fallen out of scope in createTask and is no longer valid

		// run the task
		taskEntry();

		// flag ourselves for deletion and return control to our scheduler thread
		task.thread->deleteLastFiber = true;
		return task.thread->fiber;
	}, &ctx, stackSize);

	factory->switchTo(current, fiber);
	return ctx.result;
}

// atomically wait for a runnable task
static Task* waitForTask(Scheduler* s, const RunContext* runContext)
{
	std::unique_lock<std::mutex> lock(s->runlistLock);
	while (tasklistEmpty(&s->runlist) && runContext->running())
	{
		s->runlistCond.wait(lock);
	}

	return tasklistPop(&s->runlist);
}

// main scheduler routine
static void schedRunFiber(Scheduler* s, Fiber* fiber, const RunContext* runContext)
{
	SchedulerThread thread;
	thread.fiber = fiber;
	thread.scheduler = s;
	thread.current = nullptr;

	g_currentThreadScheduler = &thread;

	for ( ; runContext->running(); )
	{
		Task* const task = waitForTask(s, runContext);
		if (!task)
		{
			continue;
		}

		Fiber* const taskFiber = task->fiber;
		task->thread = &thread;

		thread.current = task;
		thread.deleteLastFiber = false;

		task->runLock.lock();
		s->factory->switchTo(fiber, taskFiber);

		// was a delete requested
		// if so: task has gone out of scope and is no longer valid
		if (thread.deleteLastFiber)
		{
			s->factory->release(taskFiber);
		}
		else
		{
			void (*unlock)(void*) = task->unlock;
			void* unlockContext = task->unlockContext;
			task->unlock = nullptr;

			// let other threads scheduler this task
			task->runLock.unlock();

			// if we have a post unlock context, invoke it now
			if (unlock)
			{
				(unlock)(unlockContext);
			}
		}
	}

	g_currentThreadScheduler = nullptr;

	// wake up anyone waiting
	s->runlistCond.notify_all();
}

Scheduler* sched::createScheduler(FiberFactory* factory)
{
	// ensure timer context is running
	std::call_once(g_timersRunning, []() {
		std::thread(timerContextProcess, &g_timers).detach();
	});

	Scheduler* scheduler = new Scheduler;
	scheduler->factory = factory;

	return scheduler;
}

void sched::destroyScheduler(Scheduler* scheduler)
{
	delete(scheduler);
}

FiberFactory* sched::getFiberFactory(Scheduler* scheduler)
{
	return scheduler->factory;
}

void sched::run(Scheduler* scheduler, const RunContext* runContext)
{
	SchedulerThread* previousThread = g_currentThreadScheduler;
	Fiber* fiber = previousThread ? previousThread->fiber : scheduler->factory->fromCurrentThread();

	schedRunFiber(scheduler, fiber, runContext);
	g_currentThreadScheduler = previousThread;

	if (!previousThread)
	{
		scheduler->factory->releaseCurrentThread(fiber);
	}
}

void sched::runFunction(FiberFactory* factory, int nthreads, std::function<void(sched::Scheduler* scheduler)> entry)
{
	struct Context : RunContext
	{
		virtual bool running() const override
		{
			return run;
		}

		bool run = true;
	};

	Context ctx;

	Scheduler* scheduler = createScheduler(factory);

	std::vector<std::thread> threads(std::max(1, nthreads-1));

	sched::spawn(scheduler, [&entry, scheduler, &ctx, &threads]() {

		// start worker threads
		for (auto& thr : threads)
		{
			thr = std::thread(sched::run, scheduler, &ctx);
		}

		entry(scheduler);
		ctx.run = false;
	});

	run(scheduler, &ctx);

	// wait for worker threads to terminate
	for (auto& thr : threads)
	{
		thr.join();
	}

	destroyScheduler(scheduler);
}

static void suspendTask(Task* t)
{
	FiberFactory* factory = g_currentThreadScheduler->scheduler->factory;
	factory->switchTo(t->fiber, g_currentThreadScheduler->fiber);
}

Task* sched::spawn(Scheduler* scheduler, std::function<void()> entry, int stackSize)
{
	Fiber* fiber = nullptr;
	bool destroyFiber = false;
	if (g_currentThreadScheduler)
	{
		fiber = g_currentThreadScheduler->current->fiber;
	}
	else
	{
		fiber = scheduler->factory->fromCurrentThread();
		destroyFiber = true;
	}

	Task* task = createTask(scheduler->factory, fiber, std::move(entry), stackSize);

	{
		std::unique_lock<std::mutex> lock(scheduler->runlistLock);
		tasklistPush(&scheduler->runlist, task);
	}
	scheduler->runlistCond.notify_one();

	if (destroyFiber)
	{
		scheduler->factory->releaseCurrentThread(fiber);
	}

	return task;
}

Task* sched::spawn(std::function<void()> entry, int stackSize)
{
	return spawn(g_currentThreadScheduler->scheduler, std::move(entry), stackSize);
}

Task* sched::currentTask()
{
	return g_currentThreadScheduler->current;
}


void sched::yield()
{
	Task* task = g_currentThreadScheduler->current;

	wake(task);
	suspendTask(task);
}

void sched::suspendSelf()
{
	Task* task = g_currentThreadScheduler->current;
	suspendTask(task);
}

void sched::wake(Task* t)
{
	Scheduler* scheduler = t->thread->scheduler;
	{
		std::unique_lock<std::mutex> lock(scheduler->runlistLock);
		tasklistPush(&scheduler->runlist, t);
	}

	scheduler->runlistCond.notify_one();
}

void sched::suspendWithUnlock(Task* t, void unlock(void* context), void* context)
{
	t->unlock = unlock;
	t->unlockContext = context;
	suspendTask(t);
}

TimerContext* sched::timerContextCurrent()
{
	return &g_timers;
}
