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
#include "sched/platform.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include "private.h"
#include <cstdlib>
#include <windows.h>

using sched::Fiber;
using sched::FiberEntry;
using sched::Platform;


void sched::platformDefaults(Platform* platform) {
	if (nullptr == platform) {
		assert(platform != nullptr);
		return;
	}

	platform->allocate = [](size_t size) -> void* {
		return operator new(size);
	};

	platform->free = [](void* ptr, size_t size) {
		(void)size;
		operator delete(ptr);
	};

	platform->createFiberForCurrentThread = []() -> Fiber* {
		void* fiber = ConvertThreadToFiber(nullptr);
		return (Fiber*)fiber;
	};

	platform->releaseFiberForCurrentThread = [](Fiber* fiber) {
		(void)fiber;
		ConvertFiberToThread();
	};

	platform->createFiber = [](FiberEntry* entry, void* param, int stackSize) {
		assert(entry);
		assert(stackSize > 0);

		struct Context {
			FiberEntry* entry;
			void* entryParam;
			void* originatingFiber;
		};

		Context ctx;
		ctx.entry = entry;
		ctx.entryParam = param;
		ctx.originatingFiber = GetCurrentFiber();
		assert(ctx.originatingFiber);

		void* fiber = CreateFiber(stackSize, [](void* fiberParam) {
			assert(fiberParam);

			FiberEntry* entry;
			void* param;
			{
				Context* ctx = (Context*)fiberParam;
				assert(ctx->originatingFiber);

				entry = ctx->entry;
				param = ctx->entryParam;
				SwitchToFiber(ctx->originatingFiber);
			}

			assert(entry);
			entry(param);
		}, &ctx);

		// switch to the fiber so we can copy the entry function
		SwitchToFiber(fiber);
		// fiber has consumed `ctx`

		return (Fiber*)fiber;
	};

	platform->releaseFiber = [](Fiber* fiber) {
		DeleteFiber(fiber);
	};

	platform->switchToFiber = [](Fiber* from, Fiber* to) {
		(void)from;
		SwitchToFiber(to);
	};
}


#endif
