#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace Dingo::Nametag
{

	// What one Discord profile card shows. An empty string or a zero leaves that part off the card.
	struct Activity
	{
		std::string Details;
		std::string State;
		// Unix seconds; Discord counts the elapsed time up from it.
		int64_t StartUnix = 0;
		// The key of an art asset uploaded for the Discord application, and its hover text.
		std::string LargeImage;
		std::string LargeText;
		int PartySize = 0;
		int PartyMax = 0;
		// Discord shows the party's size only when it has an id.
		std::string PartyId;

		bool operator==(const Activity&) const = default;
	};

	enum class LogLevel
	{
		Info,
		Warn
	};

	// Called on the thread that calls the Client. The message never holds what Discord sent: its READY frame
	// carries the player's Discord user id, name and avatar.
	using LogFn = void(*)(LogLevel level, std::string_view message, void* user);

	struct Config
	{
		// The Discord application's id. A public number, as text: a snowflake overflows 32 bits.
		std::string AppId;
		bool Enabled = true;
		// -1 tries discord-ipc-0 to discord-ipc-9. 0 to 9 tries that one only.
		int PipeIndex = -1;
		double RetrySeconds = 10.0;
		// Discord drops activities sent faster than about one per 15 s.
		double MinSendSeconds = 15.0;
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
