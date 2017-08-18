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

#include "sched/config.h"

namespace sched {

	struct Fiber;
	typedef Fiber* FiberEntry(Fiber* self, void* context);

	struct SCHED_NO_VTABLE FiberFactory
	{
		// Convert the current thread to a fiber
		virtual Fiber* fromCurrentThread() = 0;

		// Convert the current fiber back to a thread
		virtual void releaseCurrentThread(Fiber* fiber) = 0;

		// Create a new fiber with the specified entry point
		virtual Fiber* create(FiberEntry entry, void* context, int stackSize) = 0;

		// Destroy the spcified fiber
		virtual void release(Fiber* fiber) = 0;

		// Switch execution to anther fiber. This function returns when control
		// is returned to the calling fiber
		virtual void switchTo(Fiber* from, Fiber* to) = 0;
	};

} // namespace sched
