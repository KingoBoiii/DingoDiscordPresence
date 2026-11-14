#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Dingo::DiscordPresence
{

	// Discord shows buttons to other people only, never on the player's own card.
	struct Button
	{
		// Cut to 32 UTF-16 units.
		std::string Label;
		std::string Url;

		bool operator==(const Button&) const = default;
	};

	// 1 (Streaming) needs a URL and 4 (Custom) is refused over RPC, so neither is listed.
	enum class ActivityType
	{
		Playing = 0,
		Listening = 2,
		Watching = 3,
		Competing = 5
	};

	// What one Discord profile card shows. An empty string or a zero leaves that part off the card. Details,
	// State and the two hover texts are cut to 128 UTF-16 units, Discord's limit, never inside a character.
	struct Activity
	{
		// Discord rejects the whole activity when Details or State is a single character.
		std::string Details;
		std::string State;
		// Unix seconds; Discord counts the elapsed time up from it.
		int64_t StartUnix = 0;
		// The key of an art asset uploaded for the Discord application, and its hover text.
		std::string LargeImage;
		std::string LargeText;
		std::string SmallImage;
		std::string SmallText;
		int PartySize = 0;
		int PartyMax = 0;
		// Discord shows the party's size only when it has an id.
		std::string PartyId;
		// The first two with a label and a URL are sent.
		std::vector<Button> Buttons;
		// Unix seconds; Discord counts down to it. Left off when it is not after StartUnix.
		int64_t EndUnix = 0;
		ActivityType Type = ActivityType::Playing;

		bool operator==(const Activity&) const = default;
	};

	enum class LogLevel
	{
		Info,
		Warn
	};

	// Called on the thread that calls the Client. The message never holds the body of what Discord sent: its
	// READY frame carries the player's Discord user id, name and avatar.
	using LogFn = void(*)(LogLevel level, std::string_view message, void* user);

	struct Config
	{
		// The Discord application's id. A public number, as text: a snowflake overflows 32 bits.
		// Empty is a dry run: no pipe is ever opened, and with LogFrames the log shows what would be sent.
		std::string AppId;
		bool Enabled = true;
		// -1 tries discord-ipc-0 to discord-ipc-9. 0 to 9 tries that one only.
		int PipeIndex = -1;
		double RetrySeconds = 10.0;
		// Discord drops activities sent faster than about one per 15 s.
		double MinSendSeconds = 15.0;
		// False: the first activity on a new connection, or after a Clear, goes at once, so a card is back as
		// soon as Discord is. True: two activities are never closer than MinSendSeconds, whatever happens between.
		bool KeepSendGapAcrossReconnects = false;
		// A Discord that has not read for this long is taken as hung: the link is dropped and retried.
		double WriteTimeoutSeconds = 5.0;
		// Logs every frame sent, with its JSON, and the opcode and event name of every frame received.
		bool LogFrames = false;
		LogFn Log = nullptr;
		void* LogUser = nullptr;
	};

	// Discord Rich Presence over the Discord client's local pipe. No call waits on Discord and no thread is
	// started: the game calls Update once a frame.
	class Client
	{
	public:
		// Opens nothing: the first Update connects.
		explicit Client(const Config& config);
		// Clears the card and closes the pipe. Waits at most 200 ms, and only when Discord has stopped reading.
		~Client();

		Client(const Client&) = delete;
		Client& operator=(const Client&) = delete;

		void Update();
		// Remembered, and sent by Update once Discord is ready. The latest call wins.
		void SetActivity(const Activity& activity);
		// Takes the card off the profile now, past the send gap.
		void Clear();
		// Off clears the card and disconnects. On connects at the next Update and sends the remembered activity.
		void SetEnabled(bool on);
		bool IsConnected() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

}
