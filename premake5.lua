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
		"StaticRuntime",
	}

	symbols "On"

dofile("premake5.sched.lua")
