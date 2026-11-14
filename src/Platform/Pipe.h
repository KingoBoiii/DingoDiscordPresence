#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace Dingo::DiscordPresence::Internal
{

	enum class WriteResult
	{
		Sent,
		// The pipe is full or an earlier write is still in flight: nothing was written, the link is fine.
		Dropped,
		Failed
	};

	// The Discord client's local pipe: a named pipe on Windows (WinPipe.cpp), a Unix socket on Linux
	// (PosixPipe.cpp). No call blocks, except Flush for the time it is given.
	class Pipe
	{
	public:
		Pipe();
		~Pipe();

		Pipe(const Pipe&) = delete;
		Pipe& operator=(const Pipe&) = delete;

		// Tries discord-ipc-<index> once, without waiting. `where` gets the name or path that opened.
		bool Open(int index, std::string& where);
		void Close();
		bool IsOpen() const;
		// The whole frame or nothing.
		WriteResult Write(const std::string& frame);
		// Gives a write still in flight up to `milliseconds` to finish.
		void Flush(int milliseconds);
		// Appends what Discord has sent since the last call. False when the pipe broke.
		bool ReadAvailable(std::string& into);

		static uint32_t ProcessId();

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};

}
