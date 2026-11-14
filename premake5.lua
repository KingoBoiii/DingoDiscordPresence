outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

workspace "DingoNametag"
	configurations { "Debug", "Release", "Distribution" }
	startproject "DingoNametag-Tests"
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
		defines { "DNT_PLATFORM_WINDOWS" }
		buildoptions { "/utf-8" }

	filter "system:linux"
		toolset "gcc"
		defines { "DNT_PLATFORM_LINUX" }

	filter "configurations:Debug"
		runtime "Debug"
		symbols "On"
		defines { "DNT_DEBUG" }

	filter "configurations:Release"
		runtime "Release"
		optimize "On"
		symbols "On"
		defines { "DNT_RELEASE" }

	filter "configurations:Distribution"
		runtime "Release"
		optimize "On"
		symbols "Off"
		defines { "DNT_DIST" }

	filter {}

project "DingoNametag"
	kind "StaticLib"
	-- A game or engine module may link the library into a shared object.
	pic "On"
	targetdir ("%{wks.location}/build/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/build/bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"include/DingoNametag/**.h",
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

project "DingoNametag-Tests"
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

	links { "DingoNametag" }
