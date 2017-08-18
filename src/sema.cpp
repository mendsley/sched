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

#include <atomic>
#include <cstdint>
#include <mutex>
#include "private.h"
#include "sched/scheduler.h"
#include "sched/sema.h"

using namespace sched;

namespace {

	struct Waiter
	{
		Waiter* next;
		Task* owner;
		Sema* sema;
	};

	struct Root
	{
		std::mutex lock;
		Waiter* head = nullptr;
		std::atomic<uint32_t> waiters = ATOMIC_VAR_INIT(0);
	};

} // namesapce `anonymous'

static constexpr uintptr_t c_rootTableSize = 251;
static Root g_roots[c_rootTableSize];

static Root* rootFromAddr(const void* addr)
{
	const uintptr_t index = (reinterpret_cast<uintptr_t>(addr) / 8) % c_rootTableSize;
	return &g_roots[index];
}

static bool tryAcquire(std::atomic<uint32_t>* sem)
{
	uint32_t value = sem->load();

	for (;;)
	{
		if (0 == value)
		{
			return false;
		}
		else if (sem->compare_exchange_weak(value, value - 1))
		{
			return true;
		}
	}
}

void sched::Sema::acquire()
{
	// handle the easy, non-contended case
	if (tryAcquire(&s))
	{
		return;
	}

	Task* task = currentTask();
	Root* root = rootFromAddr(this);
	for (;;)
	{
		root->lock.lock();

		// add ourselves to the tasks waiting on this root
		root->waiters.fetch_add(1);

		// acquired inbetween lock states
		if (tryAcquire(&s))
		{
			root->waiters.fetch_sub(1);
			root->lock.unlock();
			break;
		}

		// wait to be notified
		Waiter w;
		w.next = root->head;
		w.owner = task;
		w.sema = this;
		root->head = &w;

		suspendWithUnlock(task, [](void* context) {
			Root* root = static_cast<Root*>(context);
			root->lock.unlock();
		}, root);

		if (tryAcquire(&s))
		{
			// wait count decremented by Sema::release
			break;
		}
	}
}

bool sched::Sema::try_acquire()
{
	return tryAcquire(&s);
}

void sched::Sema::release()
{
	Root* root = rootFromAddr(this);
	s.fetch_add(1);

	// easy, no waiters path for this root
	if (0 == root->waiters.load())
	{
		return;
	}

	Waiter* toAwake = nullptr;
	{
		std::unique_lock<std::mutex> lock(root->lock);
		if (0 == root->waiters.load())
		{
			// another semaphore cleared all the waiters
			return;
		}

		// find a task waiting on this semaphore
		Waiter** prev = &root->head;
		for (Waiter* w = root->head; w; prev = &w->next, w = w->next)
		{
			if (w->sema == this)
			{
				root->waiters.fetch_sub(1);
				toAwake = w;

				// unlink w
				*prev = w->next;
				break;
			}
		}
	}

	if (toAwake)
	{
		wake(toAwake->owner);
	}
}
