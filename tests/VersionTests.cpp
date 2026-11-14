#include "TestFramework.h"

#include <DingoDiscordPresence/BuildInfo.h>
#include <DingoDiscordPresence/Version.h>

TEST(VersionMatchesBuildInfo)
{
	const Dingo::DiscordPresence::Version version = Dingo::DiscordPresence::GetVersion();
	CHECK(version.Major == DDP_VERSION_MAJOR);
	CHECK(version.Minor == DDP_VERSION_MINOR);
	CHECK(version.Patch == DDP_VERSION_PATCH);
	CHECK(version.Build == DDP_VERSION_BUILD);
}
