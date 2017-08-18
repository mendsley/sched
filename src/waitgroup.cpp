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

#include <cassert>
#include "sched/waitgroup.h"

using namespace sched;

void WaitGroup::add(int delta)
{
	const uint64_t shiftedDelta = static_cast<uint64_t>(delta) << 32;
	const uint64_t st = state.fetch_add(shiftedDelta) + shiftedDelta;
	const int32_t count = static_cast<int32_t>(st >> 32);
	const uint32_t waiters = static_cast<uint32_t>(st);

	assert(count >= 0 && "WaitGroup count is negative");
	assert((waiters == 0 || delta <= 0 || count != delta) && "WaitGroup invariant violation");

	// anyone to wake up
	if (count == 0 && waiters > 0)
	{
		assert(state.load() == st && "WaitGroup invariant violation");

		state.store(0);
		for (uint32_t ii = 0; ii != waiters; ++ii)
		{
			sema.release();
		}
	}
}

void WaitGroup::wait()
{
	// wait for resolution
	uint64_t st = state.load();
	for (;;)
	{
		const int32_t value = static_cast<int32_t>(st >> 32);
		if (0 == value)
		{
			return;
		}

		// add ourselves to the wait list, and block
		const uint32_t waiters = static_cast<uint32_t>(st);
		if (state.compare_exchange_weak(st, st + 1))
		{
			sema.acquire();
			return;
		}
	}
}
