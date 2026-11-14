#include "TestFramework.h"

#include "ClientTestSupport.h"

#include "DingoDiscordPresence/DiscordPresence.h"

using namespace Dingo::DiscordPresence;

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

namespace
{

	Config StatusConfig(LogRecorder& log, StatusRecorder& status)
	{
		Config config = MakeConfig(log);
		config.OnStatus = &StatusRecorder::Callback;
		config.StatusUser = &status;
		return config;
	}

}

TEST(Client_StatusNothingListeningReportsWaitingOnce)
{
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	CHECK(status.Is({}));
	CHECK(client.GetStatus() == ConnectionStatus::Waiting);

	for (int i = 0; i < 5; i++)
	{
		client.Update();
	}
	CHECK(status.Is({ ConnectionStatus::Waiting }));
}

TEST(Client_StatusSetEnabledOffWhileWaitingReportsDisabled)
{
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	client.Update();

	client.SetEnabled(false);
	CHECK(client.GetStatus() == ConnectionStatus::Disabled);
	CHECK(status.Is({ ConnectionStatus::Waiting, ConnectionStatus::Disabled }));

	client.SetEnabled(false);
	client.Update();
	CHECK(status.Is({ ConnectionStatus::Waiting, ConnectionStatus::Disabled }));
}

TEST(Client_StatusSetEnabledOffBeforeAnyUpdateReportsDisabledAtOnce)
{
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));

	client.SetEnabled(false);
	CHECK(status.Is({ ConnectionStatus::Disabled }));

	client.Update();
	CHECK(status.Is({ ConnectionStatus::Disabled }));
}

TEST(Client_StatusStartingDisabledReportsDisabledOnTheFirstUpdate)
{
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.Enabled = false;
	Client client(config);
	CHECK(status.Is({}));
	CHECK(client.GetStatus() == ConnectionStatus::Disabled);

	for (int i = 0; i < 5; i++)
	{
		client.Update();
	}
	CHECK(status.Is({ ConnectionStatus::Disabled }));
}

TEST(Client_StatusSetEnabledWithTheCurrentValueReportsNothing)
{
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	client.Update();

	client.SetEnabled(true);
	client.SetEnabled(true);
	CHECK(status.Is({ ConnectionStatus::Waiting }));
}

TEST(Client_StatusADryRunReportsDryRunOnce)
{
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.AppId.clear();
	Client client(config);
	CHECK(client.GetStatus() == ConnectionStatus::DryRun);

	for (int i = 0; i < 5; i++)
	{
		client.Update();
	}
	client.SetActivity(Activity{});
	client.Update();
	client.Clear();
	CHECK(status.Is({ ConnectionStatus::DryRun }));
}

TEST(Client_StatusADryRunThatIsDisabledReportsDryRunAndTheTogglesReportNothing)
{
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.AppId.clear();
	config.Enabled = false;
	Client client(config);
	CHECK(client.GetStatus() == ConnectionStatus::DryRun);

	client.Update();
	client.SetEnabled(true);
	client.SetEnabled(false);
	client.SetEnabled(true);
	client.Update();
	CHECK(status.Is({ ConnectionStatus::DryRun }));
	CHECK(client.GetStatus() == ConnectionStatus::DryRun);
}

TEST(Client_StatusWithoutACallbackStillAnswersGetStatus)
{
	LogRecorder log;
	Client client(MakeConfig(log));
	client.Update();
	CHECK(client.GetStatus() == ConnectionStatus::Waiting);

	client.SetEnabled(false);
	CHECK(client.GetStatus() == ConnectionStatus::Disabled);
	client.Clear();
	client.Update();
	client.SetEnabled(true);
	CHECK(client.GetStatus() == ConnectionStatus::Waiting);
}
