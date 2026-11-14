#include "DingoDiscordPresence/Version.h"
#include "DingoDiscordPresence/BuildInfo.h"

namespace Dingo::DiscordPresence
{

	Version GetVersion()
	{
		return { DDP_VERSION_MAJOR, DDP_VERSION_MINOR, DDP_VERSION_PATCH, DDP_VERSION_BUILD };
	}

}
