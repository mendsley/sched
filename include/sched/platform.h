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
#include <cstddef>

namespace sched {

	// Opaque type that `Platfrom` can treat as `void*`
	struct Fiber;


	// entry point for a fiber
	using FiberEntry = void(void* param);

	struct Platform {

		// Allocate a block of memory
		void* (*allocate)(size_t size) = nullptr;

		// Free a block of memory acquired from `allocate`
		void (*free)(void* ptr, size_t size) = nullptr;

		// Create a fiber context for the running thraed
		Fiber* (*createFiberForCurrentThread)() = nullptr;

		// Release a fiber acquired by `createFiberForCurrentThread`
		void (*releaseFiberForCurrentThread)(Fiber* fiber) = nullptr;

		// Create a new fiber
		Fiber* (*createFiber)(FiberEntry* entry, void* param, int stackSize) = nullptr;

		// Destroy a fiber
		void (*releaseFiber)(Fiber* fiber) = nullptr;

		// Switch the current running fiber
		void (*switchToFiber)(Fiber* from, Fiber* to) = nullptr;
	};

	// Set reasonable defaults for the platform. This sets the allocator
	// to use the main heap via operator new/delete  and the platform's
	// default fiber interface.
	void platformDefaults(Platform* platform);
}
