#include "TestFramework.h"

#include "ClientTestSupport.h"

#include "DingoNametag/Nametag.h"

using namespace Dingo::Nametag;

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

TEST(Client_AnEmptyAppIdIsADryRun)
{
	LogRecorder log;
	Config config = MakeConfig(log);
	config.AppId.clear();
	{
		Client client(config);
		CHECK(log.Has(LogLevel::Info, "off (no app id)"));
		client.SetEnabled(true);
		client.SetEnabled(false);
		client.SetEnabled(true);
		for (int i = 0; i < 3; i++)
		{
			client.Update();
		}
		client.SetActivity(Activity{});
		client.Update();
		CHECK(!client.IsConnected());
	}
	CHECK(log.Count("off (no app id)") == 1);
	CHECK(log.Count("not running") == 0);
	CHECK(log.Count("presence on | pipe pinned to discord-ipc-9") == 0);
	for (const auto& line : log.m_Lines)
	{
		CHECK(line.first == LogLevel::Info);
	}
}
