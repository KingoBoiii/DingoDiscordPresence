#include "TestFramework.h"

#include "ClientTestSupport.h"
#include "FakeDiscord.h"

#include "DingoDiscordPresence/DiscordPresence.h"
#include "Json.h"
#include "Platform/Pipe.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Dingo::DiscordPresence;

namespace
{

	using Clock = std::chrono::steady_clock;

	double Since(Clock::time_point from)
	{
		return std::chrono::duration<double>(Clock::now() - from).count();
	}

	void SleepFor(double seconds)
	{
		std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
	}

	void Spin(FakeDiscord& fake, Client& client, int rounds)
	{
		for (int i = 0; i < rounds; i++)
		{
			fake.Pump();
			client.Update();
			fake.Pump();
			std::this_thread::yield();
		}
	}

	void SpinFor(FakeDiscord& fake, Client& client, double seconds)
	{
		const Clock::time_point start = Clock::now();
		while (Since(start) < seconds)
		{
			Spin(fake, client, 1);
		}
	}

	template<typename Done>
	bool Until(FakeDiscord& fake, Client& client, Done done, double timeoutSeconds = 3.0)
	{
		const Clock::time_point start = Clock::now();
		while (true)
		{
			fake.Pump();
			client.Update();
			fake.Pump();
			if (done())
			{
				return true;
			}
			if (Since(start) > timeoutSeconds)
			{
				return false;
			}
			std::this_thread::yield();
		}
	}

	Config FakeConfig(LogRecorder& log)
	{
		Config config = MakeConfig(log);
		config.MinSendSeconds = 0.0;
		return config;
	}

	Activity MakeActivity(const char* details)
	{
		Activity activity;
		activity.Details = details;
		activity.State = "Graveyard";
		return activity;
	}

	int Sets(const FakeDiscord& fake)
	{
		return fake.CountFramesWith("\"details\"");
	}

	int Clears(const FakeDiscord& fake)
	{
		return fake.CountFramesWith("\"activity\":null");
	}

	constexpr int MAX_FILL_ROUNDS = 5000;

	std::string Padding(int index)
	{
		return "{\"i\":" + std::to_string(index) + ",\"pad\":\"" + std::string(3000, 'x') + "\"}";
	}

	uint32_t Pid()
	{
		return Internal::Pipe::ProcessId();
	}

	const FakeDiscord::Received* FindFrame(const FakeDiscord& fake, uint32_t opcode, const std::string& json)
	{
		for (const FakeDiscord::Received& frame : fake.Frames())
		{
			if (frame.Opcode == opcode && frame.Json == json)
			{
				return &frame;
			}
		}
		return nullptr;
	}

	int CountLinesStartingWith(const LogRecorder& log, const std::string& prefix)
	{
		int count = 0;
		for (const auto& line : log.m_Lines)
		{
			if (line.second.compare(0, prefix.size(), prefix) == 0)
			{
				count++;
			}
		}
		return count;
	}

}

TEST(Fake_TheHandshakeCarriesTheAppId)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));

	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(0) > 0; }));
	CHECK(fake.Frames()[0].Opcode == 0);
	CHECK(fake.Frames()[0].Json == "{\"v\":1,\"client_id\":\"123456789012345678\"}");
	CHECK(fake.Frames()[0].Json.find("\"v\":1") != std::string::npos);

	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(log.Has(LogLevel::Info, "ready"));
}

TEST(Fake_NothingButTheHandshakeGoesBeforeReady)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	fake.SetAutoReady(false);
	LogRecorder log;
	Client client(FakeConfig(log));
	client.SetActivity(MakeActivity("Wave 1"));

	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(0) > 0; }));
	Spin(fake, client, 30);
	CHECK(fake.Frames().size() == 1);
	CHECK(!client.IsConnected());

	fake.SendReady();
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	CHECK(fake.Frames().size() == 2);
	CHECK(fake.Frames()[1].Opcode == 1);
}

TEST(Fake_TheActivityArrivesAfterReadyAsJson)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));

	Activity activity = MakeActivity("Wave 12");
	activity.StartUnix = 1700000000;
	activity.LargeImage = "big";
	activity.LargeText = "Big";
	activity.SmallImage = "tiny";
	activity.SmallText = "Tiny";
	activity.PartyId = "lobby";
	activity.PartySize = 1;
	activity.PartyMax = 4;
	activity.Buttons = { { "Play", "https://example.test/play" } };
	client.SetActivity(activity);

	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	CHECK(FindFrame(fake, 1, Internal::ActivityJson(activity, 1, Pid())) != nullptr);
	CHECK(log.Has(LogLevel::Info, "activity \"Wave 12\" / \"Graveyard\""));
	CHECK(!log.AnyLineContains(FakeDiscord::USER_NAME));
	CHECK(!log.HasLineStartingWith("send "));
	CHECK(!log.HasLineStartingWith("recv "));
}

TEST(Fake_APingGetsAPongWithTheSameBody)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SendPing("{\"nonce\":\"abc\"}");
	fake.SendPing("{\"nonce\":\"def\"}");
	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(4) == 2; }));
	CHECK(FindFrame(fake, 4, "{\"nonce\":\"abc\"}") != nullptr);
	CHECK(FindFrame(fake, 4, "{\"nonce\":\"def\"}") != nullptr);
}

TEST(Fake_AReadySplitAcrossTwoPumpsWorks)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	fake.SetAutoReady(false);
	LogRecorder log;
	Client client(FakeConfig(log));
	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(0) > 0; }));

	const std::string ready = FakeDiscord::ReadyFrame();
	fake.SendRaw(std::string_view(ready).substr(0, 5));
	Spin(fake, client, 10);
	CHECK(!client.IsConnected());

	fake.SendRaw(std::string_view(ready).substr(5, 20));
	Spin(fake, client, 10);
	CHECK(!client.IsConnected());

	fake.SendRaw(std::string_view(ready).substr(25));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
}

TEST(Fake_AnOversizeLengthDropsTheLink)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SendRaw(std::string("\x01\0\0\0\xFF\xFF\xFF\x7F", 8));
	REQUIRE(Until(fake, client, [&] { return log.Has(LogLevel::Warn, "lost (bad frame)"); }));
}

TEST(Fake_CloseDropsTheLinkAndTheClientReconnectsAndResends)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	fake.SendClose();
	REQUIRE(Until(fake, client, [&] { return log.Has(LogLevel::Warn, "lost (closed by discord)"); }));
	REQUIRE(Until(fake, client, [&] { return fake.Connections() == 2 && Sets(fake) == 2; }));
	CHECK(fake.CountOpcode(0) == 2);
}

TEST(Fake_TheSendGapHoldsAcrossAReconnect)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 0.4;
	config.KeepSendGapAcrossReconnects = true;
	Client client(config);
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	const Clock::time_point first = Clock::now();

	fake.SendClose();
	REQUIRE(Until(fake, client, [&] { return fake.Connections() == 2 && client.IsConnected(); }));
	if (Since(first) < 0.3)
	{
		CHECK(Sets(fake) == 1);
	}

	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
	CHECK(Since(first) >= 0.35);
}

TEST(Fake_ByDefaultANewConnectionSendsAtOnceAndTheGapHoldsWithinOne)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 30.0;
	Client client(config);
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.SetActivity(MakeActivity("Wave 2"));
	Spin(fake, client, 30);
	CHECK(Sets(fake) == 1);

	fake.SendClose();
	REQUIRE(Until(fake, client, [&] { return fake.Connections() == 2 && Sets(fake) == 2; }));
	CHECK(fake.Frames().back().Json.find("Wave 2") != std::string::npos);
}

TEST(Fake_TheSameActivityTwiceSendsOnce)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	const Activity activity = MakeActivity("Wave 1");
	client.SetActivity(activity);
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.SetActivity(activity);
	Spin(fake, client, 20);
	CHECK(Sets(fake) == 1);

	client.SetActivity(MakeActivity("Wave 2"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
}

TEST(Fake_AnEndAndATypeArriveInTheActivity)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));

	Activity activity = MakeActivity("Wave 12");
	activity.StartUnix = 1700000000;
	activity.EndUnix = 1700000600;
	activity.Type = ActivityType::Competing;
	client.SetActivity(activity);

	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	CHECK(FindFrame(fake, 1, Internal::ActivityJson(activity, 1, Pid())) != nullptr);
	const std::string& json = fake.Frames().back().Json;
	CHECK(json.find("\"type\":5") != std::string::npos);
	CHECK(json.find("\"end\":1700000600") != std::string::npos);
}

TEST(Fake_ChangingOnlyTheEndResends)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 0.05;
	Client client(config);
	Activity activity = MakeActivity("Wave 1");
	activity.StartUnix = 1700000000;
	activity.EndUnix = 1700000600;
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	activity.EndUnix = 1700000900;
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
	CHECK(fake.Frames().back().Json.find("\"end\":1700000900") != std::string::npos);
}

TEST(Fake_ClearSendsANullActivity)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.Clear();
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1; }));
	CHECK(FindFrame(fake, 1, Internal::ClearJson(2, Pid())) != nullptr);
	CHECK(log.Has(LogLevel::Info, "cleared"));
}

TEST(Fake_ClearThenTheSameActivitySendsItAgain)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 0.05;
	Client client(config);
	const Activity activity = MakeActivity("Wave 1");
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.Clear();
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1; }));
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
}

TEST(Fake_ClearGoesOutPastTheSendGap)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 30.0;
	config.KeepSendGapAcrossReconnects = true;
	Client client(config);
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.Clear();
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1; }));

	client.SetActivity(MakeActivity("Wave 2"));
	Spin(fake, client, 30);
	CHECK(Sets(fake) == 1);
}

TEST(Fake_ByDefaultTheActivityAfterAClearGoesAtOnce)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 30.0;
	Client client(config);
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.Clear();
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1; }));

	client.SetActivity(MakeActivity("Wave 2"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
}

TEST(Fake_DestroyingTheClientClearsTheCard)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	{
		Client client(FakeConfig(log));
		client.SetActivity(MakeActivity("Wave 1"));
		REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	}

	for (int i = 0; i < 10; i++)
	{
		fake.Pump();
		std::this_thread::yield();
	}
	CHECK(Clears(fake) == 1);
	CHECK(!fake.Frames().empty() && fake.Frames().back().Json.find("\"activity\":null") != std::string::npos);
}

TEST(Fake_SetEnabledOffClearsAndClosesAndOnReconnectsAndResends)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.MinSendSeconds = 0.05;
	Client client(config);
	client.SetActivity(MakeActivity("Wave 1"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	client.SetEnabled(false);
	CHECK(!client.IsConnected());
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1 && !fake.HasClient(); }));
	Spin(fake, client, 20);
	CHECK(fake.Connections() == 1);

	SleepFor(0.06);
	client.SetEnabled(true);
	REQUIRE(Until(fake, client, [&] { return fake.Connections() == 2 && Sets(fake) == 2; }));
}

TEST(Fake_AServerThatStopsReadingTimesTheLinkOut)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.WriteTimeoutSeconds = 0.2;
	config.RetrySeconds = 30.0;
	Client client(config);
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SetReading(false);
	const std::string body = "{\"pad\":\"" + std::string(3000, 'x') + "\"}";
	const Clock::time_point start = Clock::now();
	double slowest = 0.0;
	bool lost = false;
	while (!lost && Since(start) < 5.0)
	{
		fake.SendPing(body);
		fake.SendPing(body);
		fake.Pump();
		const Clock::time_point before = Clock::now();
		client.Update();
		slowest = (std::max)(slowest, Since(before));
		lost = log.Has(LogLevel::Warn, "lost (write timed out)");
		SleepFor(0.001);
	}

	CHECK(lost);
	CHECK(slowest < 0.5);
	CHECK(!client.IsConnected());
}

TEST(Fake_ABlockedPongAndClearAreRetriedWhenTheServerReadsAgain)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.WriteTimeoutSeconds = 30.0;
	Client client(config);
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SetReading(false);
	bool blocked = false;
	for (int i = 0; i < MAX_FILL_ROUNDS && !blocked; i++)
	{
		fake.SendPing(Padding(i));
		client.Update();
		const int cleared = log.Count("cleared");
		client.Clear();
		blocked = log.Count("cleared") == cleared;
	}
	REQUIRE(blocked);

	const std::string last = Padding(MAX_FILL_ROUNDS);
	fake.SendPing(last);
	client.Update();
	client.SetActivity(MakeActivity("Never sent"));
	client.Clear();
	client.Update();
	const int clearedWhileBlocked = log.Count("cleared");

	fake.SetReading(true);
	REQUIRE(Until(fake, client, [&] { return FindFrame(fake, 4, last) != nullptr && log.Count("cleared") > clearedWhileBlocked; }));
	REQUIRE(Until(fake, client, [&] { return Clears(fake) == log.Count("cleared"); }));
	Spin(fake, client, 20);
	CHECK(Clears(fake) == log.Count("cleared"));
	CHECK(Sets(fake) == 0);
	CHECK(!log.AnyLineContains("lost"));
}

TEST(Fake_AnActivityTakenBackAfterADroppedWriteDoesNotTimeTheLinkOut)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.WriteTimeoutSeconds = 0.25;
	Client client(config);

	Activity shown = MakeActivity("Shown");
	client.SetActivity(shown);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	fake.SetReading(false);
	bool dropped = false;
	for (int i = 0; i < MAX_FILL_ROUNDS && !dropped; i++)
	{
		Activity next = MakeActivity(("Next " + std::to_string(i)).c_str());
		next.State = std::string(100, 'x');
		const std::string line = "activity \"" + next.Details + "\" / \"" + next.State + "\"";
		client.SetActivity(next);
		client.Update();
		if (log.Count(line) > 0)
		{
			shown = next;
		}
		else
		{
			dropped = true;
		}
	}
	REQUIRE(dropped);

	client.SetActivity(shown);
	client.Update();
	fake.SetReading(true);

	const Clock::time_point start = Clock::now();
	while (Since(start) < 0.7)
	{
		Spin(fake, client, 1);
	}
	CHECK(!log.Has(LogLevel::Warn, "lost (write timed out)"));
	CHECK(client.IsConnected());
}

TEST(Fake_NothingQueuedOrTimedCarriesOverToANewConnection)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.WriteTimeoutSeconds = 0.3;
	config.RetrySeconds = 0.4;
	Client client(config);
	client.SetActivity(MakeActivity("Before"));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	fake.SetReading(false);
	bool blocked = false;
	for (int i = 0; i < MAX_FILL_ROUNDS && !blocked; i++)
	{
		fake.SendPing(Padding(i));
		client.Update();
		const int cleared = log.Count("cleared");
		client.Clear();
		blocked = log.Count("cleared") == cleared;
	}
	REQUIRE(blocked);
	fake.SendPing(Padding(MAX_FILL_ROUNDS));
	client.Update();
	client.SetActivity(MakeActivity("After"));
	client.Update();

	fake.SendClose();
	REQUIRE(Until(fake, client, [&] { return log.Has(LogLevel::Warn, "lost (closed by discord)"); }));
	CHECK(!log.Has(LogLevel::Warn, "lost (write timed out)"));
	fake.DropClient();
	fake.SetReading(true);

	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(0) == 2; }));
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 2; }));
	Spin(fake, client, 20);

	size_t handshakes = 0;
	size_t second = 0;
	for (size_t i = 0; i < fake.Frames().size(); i++)
	{
		if (fake.Frames()[i].Opcode == 0 && ++handshakes == 2)
		{
			second = i;
		}
	}
	REQUIRE(handshakes == 2);
	REQUIRE(fake.Frames().size() == second + 2);
	CHECK(fake.Frames()[second + 1].Opcode == 1);
	CHECK(fake.Frames()[second + 1].Json.find("\"details\":\"After\"") != std::string::npos);
	CHECK(!log.Has(LogLevel::Warn, "lost (write timed out)"));
	CHECK(client.IsConnected());
}

TEST(Fake_LogFramesLogsFramesButNothingOfWhatDiscordSent)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.LogFrames = true;
	Client client(config);
	const Activity activity = MakeActivity("Wave 1");
	client.SetActivity(activity);
	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));

	const std::string secret = std::string("{\"user\":\"") + FakeDiscord::USER_NAME + "\"}";
	fake.SendPing(secret);
	fake.SendFrame(1, std::string("{\"cmd\":\"SET_ACTIVITY\",\"data\":{\"username\":\"") + FakeDiscord::USER_NAME
		+ "\"},\"evt\":null,\"nonce\":\"1\"}");
	fake.SendFrame(1, std::string("{\"evt\":\"") + FakeDiscord::USER_NAME + "\"}");
	fake.SendFrame(7, secret);
	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(4) == 1; }));
	Spin(fake, client, 10);

	CHECK(!log.AnyLineContains(FakeDiscord::USER_NAME));
	CHECK(log.HasLine("recv FRAME READY"));
	CHECK(log.HasLine("recv PING"));
	CHECK(log.HasLine("recv FRAME SET_ACTIVITY"));
	CHECK(log.HasLine("recv 7"));
	CHECK(log.HasLine("send PONG"));
	CHECK(log.HasLine("send HANDSHAKE {\"v\":1,\"client_id\":\"123456789012345678\"}"));
	CHECK(log.HasLine("send SET_ACTIVITY " + Internal::ActivityJson(activity, 1, Pid())));
	CHECK(log.HasLine("recv FRAME"));
	CHECK(CountLinesStartingWith(log, "recv FRAME ") == 2);
}

TEST(Fake_ADryRunOpensNothingAndLogsTheFrame)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.AppId.clear();
	config.LogFrames = true;
	config.MinSendSeconds = 0.3;
	Client client(config);

	const Activity first = MakeActivity("Wave 1");
	client.SetActivity(first);
	SpinFor(fake, client, 0.05);
	CHECK(fake.Connections() == 0);
	CHECK(!fake.HasClient());
	CHECK(!client.IsConnected());
	CHECK(log.HasLine("send (dry run) SET_ACTIVITY " + Internal::ActivityJson(first, 1, Pid())));

	client.SetActivity(MakeActivity("Wave 2"));
	SpinFor(fake, client, 0.05);
	CHECK(CountLinesStartingWith(log, "send (dry run) SET_ACTIVITY {\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"2\"") == 0);

	SleepFor(0.25);
	Spin(fake, client, 5);
	CHECK(CountLinesStartingWith(log, "send (dry run) SET_ACTIVITY {\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"2\"") == 1);

	client.Clear();
	CHECK(log.HasLine("send (dry run) SET_ACTIVITY " + Internal::ClearJson(3, Pid())));

	client.SetEnabled(false);
	client.SetEnabled(true);
	Spin(fake, client, 10);
	CHECK(fake.Connections() == 0);
	CHECK(!client.IsConnected());
	CHECK(!log.HasLineStartingWith("send HANDSHAKE"));
	CHECK(!log.HasLineStartingWith("connected"));
}

TEST(Fake_ADryRunThatIsDisabledSaysNothing)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.AppId.clear();
	config.LogFrames = true;
	config.Enabled = false;
	{
		Client client(config);
		client.SetActivity(MakeActivity("Wave 1"));
		Spin(fake, client, 10);
		client.Clear();
		client.Update();
	}
	CHECK(log.m_Lines.size() == 1);
	CHECK(log.HasLine("off (no app id)"));
	CHECK(fake.Connections() == 0);
}

TEST(Fake_ADisabledClientOpensNothing)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Config config = FakeConfig(log);
	config.Enabled = false;
	{
		Client client(config);
		client.SetActivity(MakeActivity("Wave 1"));
		Spin(fake, client, 20);
		CHECK(fake.Connections() == 0);
	}
	CHECK(fake.Frames().empty());
}

namespace
{

	Config StatusConfig(LogRecorder& log, StatusRecorder& status)
	{
		Config config = FakeConfig(log);
		config.OnStatus = &StatusRecorder::Callback;
		config.StatusUser = &status;
		return config;
	}

	struct StatusActor
	{
		Client* m_Client = nullptr;
		void (*m_Act)(Client& client, ConnectionStatus status) = nullptr;
		std::vector<ConnectionStatus> m_Seen;

		static void Callback(ConnectionStatus status, void* user)
		{
			StatusActor* self = static_cast<StatusActor*>(user);
			self->m_Seen.push_back(status);
			if (self->m_Act)
			{
				self->m_Act(*self->m_Client, status);
			}
		}
	};

	Config ActorConfig(LogRecorder& log, StatusActor& actor)
	{
		Config config = FakeConfig(log);
		config.OnStatus = &StatusActor::Callback;
		config.StatusUser = &actor;
		return config;
	}

	constexpr ConnectionStatus WAITING = ConnectionStatus::Waiting;
	constexpr ConnectionStatus CONNECTED = ConnectionStatus::Connected;
	constexpr ConnectionStatus DISABLED = ConnectionStatus::Disabled;

}

TEST(Fake_StatusReportsWaitingOnTheFirstUpdateThenConnected)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));

	fake.Pump();
	client.Update();
	CHECK(status.Is({ WAITING }));
	CHECK(client.GetStatus() == WAITING);

	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(status.Is({ WAITING, CONNECTED }));
	CHECK(client.GetStatus() == CONNECTED);
}

TEST(Fake_StatusStaysWaitingUntilReady)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	fake.SetAutoReady(false);
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));

	REQUIRE(Until(fake, client, [&] { return fake.CountOpcode(0) > 0; }));
	Spin(fake, client, 30);
	CHECK(status.Is({ WAITING }));

	fake.SendReady();
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(status.Is({ WAITING, CONNECTED }));
}

TEST(Fake_StatusCloseReportsWaitingAndAReconnectReportsConnectedAgain)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SendClose();
	REQUIRE(Until(fake, client, [&] { return status.m_Statuses.size() == 4; }));
	CHECK(status.Is({ WAITING, CONNECTED, WAITING, CONNECTED }));
	CHECK(fake.Connections() == 2);
}

TEST(Fake_StatusTheFakeDroppingTheConnectionReportsWaiting)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.RetrySeconds = 30.0;
	Client client(config);
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.DropClient();
	REQUIRE(Until(fake, client, [&] { return status.m_Statuses.size() == 3; }));
	CHECK(status.Is({ WAITING, CONNECTED, WAITING }));
	CHECK(client.GetStatus() == WAITING);
}

TEST(Fake_StatusAnOversizeLengthReportsWaiting)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.RetrySeconds = 30.0;
	Client client(config);
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SendRaw(std::string("\x01\0\0\0\xFF\xFF\xFF\x7F", 8));
	REQUIRE(Until(fake, client, [&] { return log.Has(LogLevel::Warn, "lost (bad frame)"); }));
	CHECK(status.Is({ WAITING, CONNECTED, WAITING }));
}

TEST(Fake_StatusAWriteTimeoutReportsWaiting)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.WriteTimeoutSeconds = 0.2;
	config.RetrySeconds = 30.0;
	Client client(config);
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	fake.SetReading(false);
	const std::string body = "{\"pad\":\"" + std::string(3000, 'x') + "\"}";
	const Clock::time_point start = Clock::now();
	bool lost = false;
	while (!lost && Since(start) < 5.0)
	{
		fake.SendPing(body);
		fake.SendPing(body);
		fake.Pump();
		client.Update();
		lost = log.Has(LogLevel::Warn, "lost (write timed out)");
		SleepFor(0.001);
	}

	REQUIRE(lost);
	CHECK(status.Is({ WAITING, CONNECTED, WAITING }));
}

TEST(Fake_StatusDisablingWhileConnectedReportsDisabledAndEnablingReportsWaitingThenConnected)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	client.SetEnabled(false);
	CHECK(client.GetStatus() == DISABLED);
	CHECK(status.Is({ WAITING, CONNECTED, DISABLED }));
	REQUIRE(Until(fake, client, [&] { return !fake.HasClient(); }));

	client.SetEnabled(true);
	CHECK(status.Is({ WAITING, CONNECTED, DISABLED, WAITING }));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(status.Is({ WAITING, CONNECTED, DISABLED, WAITING, CONNECTED }));
}

TEST(Fake_StatusSetEnabledWithTheCurrentValueWhileConnectedReportsNothing)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	client.SetEnabled(true);
	CHECK(status.Is({ WAITING, CONNECTED }));
}

TEST(Fake_StatusStartingDisabledReportsDisabledOnTheFirstUpdate)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.Enabled = false;
	Client client(config);

	Spin(fake, client, 10);
	CHECK(status.Is({ DISABLED }));
	CHECK(fake.Connections() == 0);
}

TEST(Fake_StatusADryRunReportsDryRunOnceAndItsTogglesReportNothing)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Config config = StatusConfig(log, status);
	config.AppId.clear();
	Client client(config);

	Spin(fake, client, 10);
	client.SetEnabled(false);
	client.SetEnabled(true);
	Spin(fake, client, 10);
	CHECK(status.Is({ ConnectionStatus::DryRun }));
	CHECK(fake.Connections() == 0);
}

TEST(Fake_StatusTenUpdatesWhileConnectedAddNoReport)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	Client client(StatusConfig(log, status));
	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));

	Spin(fake, client, 10);
	CHECK(status.Is({ WAITING, CONNECTED }));
}

TEST(Fake_StatusPassesStatusUserThrough)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	int calls = 0;
	void* seen = nullptr;
	struct Probe
	{
		int* m_Calls;
		void** m_Seen;
	} probe{ &calls, &seen };

	Config config = FakeConfig(log);
	config.OnStatus = [](ConnectionStatus, void* user)
	{
		Probe* self = static_cast<Probe*>(user);
		++*self->m_Calls;
		*self->m_Seen = user;
	};
	config.StatusUser = &probe;
	Client client(config);

	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(calls == 2);
	CHECK(seen == &probe);
}

TEST(Fake_StatusWithoutACallbackStillAnswersGetStatus)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	Client client(FakeConfig(log));
	CHECK(client.GetStatus() == WAITING);

	REQUIRE(Until(fake, client, [&] { return client.IsConnected(); }));
	CHECK(client.GetStatus() == CONNECTED);

	client.SetEnabled(false);
	CHECK(client.GetStatus() == DISABLED);
}

TEST(Fake_StatusDestroyingAConnectedClientMakesNoCall)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusRecorder status;
	{
		Client client(StatusConfig(log, status));
		client.SetActivity(MakeActivity("Wave 1"));
		REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
		CHECK(status.Is({ WAITING, CONNECTED }));
	}
	CHECK(status.Is({ WAITING, CONNECTED }));
}

TEST(Fake_StatusSetActivityInsideTheCallbackIsSent)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusActor actor;
	actor.m_Act = [](Client& client, ConnectionStatus status)
	{
		if (status == CONNECTED)
		{
			client.SetActivity(MakeActivity("From callback"));
		}
	};
	Client client(ActorConfig(log, actor));
	actor.m_Client = &client;

	REQUIRE(Until(fake, client, [&] { return Sets(fake) == 1; }));
	CHECK(fake.Frames().back().Json.find("\"details\":\"From callback\"") != std::string::npos);
}

TEST(Fake_StatusClearInsideTheCallbackSendsTheNullActivity)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusActor actor;
	actor.m_Act = [](Client& client, ConnectionStatus status)
	{
		if (status == CONNECTED)
		{
			client.Clear();
		}
	};
	Client client(ActorConfig(log, actor));
	actor.m_Client = &client;

	REQUIRE(Until(fake, client, [&] { return Clears(fake) == 1; }));
	CHECK(Sets(fake) == 0);
	CHECK(actor.m_Seen.size() == 2);
}

TEST(Fake_StatusSetEnabledOffInsideTheCallbackMakesNoNestedCallAndTheNextUpdateReportsDisabled)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusActor actor;
	actor.m_Act = [](Client& client, ConnectionStatus status)
	{
		if (status == CONNECTED)
		{
			client.SetEnabled(false);
		}
	};
	Client client(ActorConfig(log, actor));
	actor.m_Client = &client;

	REQUIRE(Until(fake, client, [&] { return actor.m_Seen.size() >= 2; }));
	CHECK(actor.m_Seen.size() == 2);
	CHECK(client.GetStatus() == DISABLED);

	client.Update();
	REQUIRE(actor.m_Seen.size() == 3);
	CHECK(actor.m_Seen[2] == DISABLED);
	Spin(fake, client, 5);
	CHECK(actor.m_Seen.size() == 3);
}

TEST(Fake_StatusACallbackThatTogglesOnEveryCallTerminatesWithOneReportPerUpdate)
{
	FakeDiscord fake;
	REQUIRE(fake.Ok());
	LogRecorder log;
	StatusActor actor;
	actor.m_Act = [](Client& client, ConnectionStatus status)
	{
		client.SetEnabled(status == DISABLED);
	};
	Client client(ActorConfig(log, actor));
	actor.m_Client = &client;

	for (int i = 1; i <= 10; i++)
	{
		fake.Pump();
		client.Update();
		CHECK(actor.m_Seen.size() == static_cast<size_t>(i));
	}
	for (size_t i = 0; i < actor.m_Seen.size(); i++)
	{
		CHECK(actor.m_Seen[i] == (i % 2 == 0 ? WAITING : DISABLED));
	}
}
