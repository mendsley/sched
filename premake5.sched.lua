local SCHED_DIR = path.getdirectory(_SCRIPT) .. "/"

project "sched"
	kind "StaticLib"

	files {
		SCHED_DIR .. "src/**",
		SCHED_DIR .. "include/**",
	}

	includedirs {
		SCHED_DIR .. "include/",
	}
