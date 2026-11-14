outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

workspace "DingoDiscordPresence"
	configurations { "Debug", "Release", "Distribution" }
	startproject "DingoDiscordPresence-Tests"
	language "C++"
	cppdialect "C++20"
	staticruntime "Off"
	architecture "x86_64"
	multiprocessorcompile "On"
	debugformat "c7"
	warnings "Extra"
	fatalwarnings { "All" }

	filter "system:windows"
		toolset "v145"
		systemversion "latest"
		defines { "DDP_PLATFORM_WINDOWS" }
		buildoptions { "/utf-8" }

	filter "system:linux"
		toolset "gcc"
		defines { "DDP_PLATFORM_LINUX" }

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		defines { "DDP_DEBUG" }

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "On"
		defines { "DDP_RELEASE" }

	filter "configurations:Distribution"
		runtime "Release"
		optimize "On"
		symbols "Off"
		defines { "DDP_DIST" }

	filter {}

project "DingoDiscordPresence"
	kind "StaticLib"
	-- A game or engine module may link the library into a shared object.
	pic "On"
	targetdir ("%{wks.location}/build/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/build/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"include/DingoDiscordPresence/**.h",
		"src/**.h",
		"src/**.cpp"
	}

	includedirs
	{
		"include",
		"src"
	}

	filter "system:windows"
		removefiles { "src/Platform/PosixPipe.cpp" }

	filter "system:linux"
		removefiles { "src/Platform/WinPipe.cpp" }

	filter {}

project "DingoDiscordPresence-Tests"
	kind "ConsoleApp"
	location "tests"
	targetdir ("%{wks.location}/build/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/build/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"tests/**.h",
		"tests/**.cpp"
	}

	includedirs
	{
		"include",
		"src"
	}

	links { "DingoDiscordPresence" }
