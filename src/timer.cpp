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

#include <chrono>
#include "private.h"
#include "sched/scheduler.h"
#include "sched/timer.h"

using namespace sched;

typedef std::chrono::high_resolution_clock timer_clock;

struct sched::TimerContext::Timer
{
	timer_clock::time_point when;
	Task* task;
	int internalHeapIndex;
};

// timer heap is a quad-child heap (each node has 4 child nodes, instead of two)
// bubbleUp takes a timer and recusively swaps it with it's parent until
// its parent wakes up before the timer
static void heapBubbleUp(TimerContext* ctx, int timerIndex)
{
	TimerContext::Timer** timers = ctx->timers.data();

	TimerContext::Timer* const temp = timers[timerIndex];
	const auto when = temp->when;

	while (timerIndex > 0)
	{
		// get parent (at index/4)
		const int parentIndex = (timerIndex - 1) / 4;
		if (when >= timers[parentIndex]->when)
		{
			// if parent wakes up before us, we're done
			break;
		}

		// swap myself with my parent
		timers[timerIndex] = timers[parentIndex];
		timers[timerIndex]->internalHeapIndex = timerIndex;
		timers[parentIndex] = temp;
		timers[parentIndex]->internalHeapIndex = parentIndex;

		// continue checking parents
		timerIndex = parentIndex;
	}
}

static void heapBubbleDown(TimerContext* ctx, int timerIndex)
{
	TimerContext::Timer** timers = ctx->timers.data();
	const int ntimers = static_cast<int>(ctx->timers.size());

	TimerContext::Timer* const temp = timers[timerIndex];
	const auto when = temp->when;

	for (;;)
	{
		// are there any children to push down into?
		int candidateIndex = timerIndex * 4 + 1;
		if (candidateIndex >= ntimers)
		{
			break;
		}

		// should we swap with our second child rather than the initial?
		auto candidateWhen = timers[candidateIndex]->when;
		if (candidateIndex + 1 < ntimers && timers[candidateIndex + 1]->when < when)
		{
			candidateWhen = timers[candidateIndex + 1]->when;
			++candidateIndex;
		}

		// check 3rd child (our node's immediate sibling)
		int siblingIndex = timerIndex + 2;
		if (siblingIndex < ntimers)
		{
			// should we swap with our 4th child rather than the 3rd?
			auto siblingWhen = timers[siblingIndex]->when;
			if (siblingIndex + 1 < ntimers && timers[siblingIndex + 1]->when < siblingWhen)
			{
				siblingWhen = timers[siblingIndex + 1]->when;
				++siblingIndex;
			}

			// should we swap with our sibling? or with our child?
			if (siblingWhen < candidateWhen)
			{
				candidateWhen = siblingWhen;
				candidateIndex = siblingIndex;
			}
		}

		// if our timer expires after the best candidate, we're in the correct index
		if (candidateWhen >= when)
		{
			break;
		}

		// swap with the candidate
		timers[timerIndex] = timers[candidateIndex];
		timers[timerIndex]->internalHeapIndex = timerIndex;
		timers[candidateIndex] = temp;
		timers[candidateIndex]->internalHeapIndex = candidateIndex;

		// continue using the previous candidate as the new root
		timerIndex = candidateIndex;
	}
}

static void addWithLock(TimerContext* ctx, TimerContext::Timer* timer)
{
	// insert into the timer heap
	timer->internalHeapIndex = static_cast<int>(ctx->timers.size());
	ctx->timers.push_back(timer);
	heapBubbleUp(ctx, timer->internalHeapIndex);

	// is the timer the new earliest timeout? if so, wake up the processing
	// thread
	if (timer->internalHeapIndex == 0)
	{
		ctx->cond.notify_one();
	}
}

void sched::timerContextProcess(TimerContext* ctx)
{
	for (;;)
	{
		std::unique_lock<std::mutex> lock(ctx->lock);
		auto now = timer_clock::now();
		timer_clock::duration delta;

		// loop as long as we have a timer that is expired
		for (;;)
		{
			TimerContext::Timer** timers = ctx->timers.data();
			const int ntimers = static_cast<int>(ctx->timers.size());
			if (ntimers == 0)
			{
				// no timers
				delta = delta.max();
				break;
			}

			// is the first timer ready to expire?
			TimerContext::Timer* t = timers[0];
			delta = t->when - now;
			if (delta > delta.zero())
			{
				// timer has not expired
				break;
			}

			// remove the timer from the heap
			const int lastIndex = ntimers - 1;
			if (lastIndex > 0)
			{
				// pull the latest timer to the head (we'll push it down in a moment)
				timers[0] = timers[lastIndex];
				timers[0]->internalHeapIndex = 0;
			}

			ctx->timers.pop_back();

			// ensure the root node is correct
			if (lastIndex > 0)
			{
				heapBubbleDown(ctx, 0);
			}

			// mark the timer as removed
			t->internalHeapIndex = -1;

			// wake the timer's owning task
			wake(t->task);
		}

		// wait for a wakeup, or for the next timer to exipre
		if (delta == delta.max())
		{
			ctx->cond.wait(lock);
		}
		else
		{
			ctx->cond.wait_for(lock, delta);
		}
	}
}

void sched::sleepMS(int ms)
{
	Task* task = currentTask();

	TimerContext::Timer timer;
	timer.when = timer_clock::now() + std::chrono::milliseconds(ms);
	timer.task = task;

	TimerContext* timers = timerContextCurrent();
	timers->lock.lock();

	addWithLock(timers, &timer);

	// suspend task, then unlock the timer context
	suspendWithUnlock(task, [](void* context) {
		TimerContext* timers = static_cast<TimerContext*>(context);
		timers->lock.unlock();
	}, timers);
}
