#include "TestFramework.h"

#include "DingoNametag/Nametag.h"

#include <string>
#include <utility>
#include <vector>

using namespace Dingo::Nametag;

namespace
{

	struct LogRecorder
	{
		std::vector<std::pair<LogLevel, std::string>> m_Lines;

		static void Callback(LogLevel level, std::string_view message, void* user)
		{
			static_cast<LogRecorder*>(user)->m_Lines.emplace_back(level, std::string(message));
		}

		int Count(const std::string& message) const
		{
			int count = 0;
			for (const auto& line : m_Lines)
			{
				if (line.second == message)
				{
					count++;
				}
			}
			return count;
		}

		bool Has(LogLevel level, const std::string& message) const
		{
			for (const auto& line : m_Lines)
			{
				if (line.first == level && line.second == message)
				{
					return true;
				}
			}
			return false;
		}
	};

	Config MakeConfig(LogRecorder& log)
	{
		Config config;
		config.AppId = "123456789012345678";
		config.PipeIndex = 9;
		config.RetrySeconds = 0.0;
		config.Log = &LogRecorder::Callback;
		config.LogUser = &log;
		return config;
	}

}

TEST(Client_DisabledNeverConnects)
{
	LogRecorder log;
	Config config = MakeConfig(log);
	config.Enabled = false;
	{
		Client client(config);
		for (int i = 0; i < 5; i++)
		{
			client.Update();
		}
		Activity activity;
		activity.Details = "x";
		client.SetActivity(activity);
		client.Update();
		CHECK(!client.IsConnected());
	}
	CHECK(log.Count("not running") == 0);
}

TEST(Client_NothingListening)
{
	LogRecorder log;
	{
		Client client(MakeConfig(log));
		for (int i = 0; i < 5; i++)
		{
			client.Update();
		}
		CHECK(!client.IsConnected());

		Activity activity;
		activity.Details = "Wave 1";
		activity.State = "Graveyard";
		client.SetActivity(activity);
		client.SetActivity(activity);
		client.Update();
		client.Clear();
		client.SetEnabled(false);
		client.Update();
		client.SetEnabled(true);
		client.Update();
		client.Update();
		CHECK(!client.IsConnected());
	}
	CHECK(log.Count("not running") == 1);
	CHECK(log.Count("ready") == 0);
}

TEST(Client_LogsThePinnedPipe)
{
	LogRecorder log;
	{
		Client client(MakeConfig(log));
	}
	CHECK(log.Has(LogLevel::Info, "presence on | pipe pinned to discord-ipc-9"));
}

TEST(Client_AnEmptyAppIdStaysOff)
{
	LogRecorder log;
	Config config = MakeConfig(log);
	config.AppId.clear();
	{
		Client client(config);
		CHECK(log.Has(LogLevel::Warn, "no app id"));
		client.SetEnabled(true);
		for (int i = 0; i < 3; i++)
		{
			client.Update();
		}
		client.SetActivity(Activity{});
		client.Update();
		CHECK(!client.IsConnected());
	}
	CHECK(log.Count("no app id") == 1);
	CHECK(log.Count("not running") == 0);
}
