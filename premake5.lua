workspace "sched"

	configurations {
		"Debug",
		"Release",
		"NoMinimalRebuild",
	}

	platforms {
		"x64",
	}

	flags {
		"C++14",
		"StaticRuntime",
	}

	symbols "On"

dofile("premake5.sched.lua")
