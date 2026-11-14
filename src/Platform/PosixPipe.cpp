#include "Platform/Pipe.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Dingo::DiscordPresence::Internal
{

	namespace
	{

		constexpr size_t READ_MAX = 1u << 16;

		std::string SocketFolder()
		{
			for (const char* variable : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" })
			{
				if (const char* value = std::getenv(variable); value && value[0] != '\0')
				{
					return value;
				}
			}
			return "/tmp";
		}

	}

	struct Pipe::State
	{
		int m_Socket = -1;
	};

	Pipe::Pipe()
		: m_State(std::make_unique<State>())
	{
	}

	Pipe::~Pipe()
	{
		Close();
	}

	// Non-blocking and close-on-exec, so a child process never inherits the socket. A Unix socket connects at once or
	// fails at once (ENOENT, ECONNREFUSED, EAGAIN for a full backlog): there is nothing to wait for.
	bool Pipe::Open(int index, std::string& where)
	{
		const std::string folder = SocketFolder();
		const std::string name = "discord-ipc-" + std::to_string(index);
		for (const char* sub : { "", "app/com.discordapp.Discord/", "snap.discord/" })
		{
			const std::string path = folder + "/" + sub + name;

			sockaddr_un address{};
			address.sun_family = AF_UNIX;
			if (path.size() >= sizeof(address.sun_path))
			{
				continue;
			}
			std::memcpy(address.sun_path, path.c_str(), path.size() + 1);

			const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
			if (fd < 0)
			{
				continue;
			}
			if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
			{
				close(fd);
				continue;
			}

			m_State->m_Socket = fd;
			where = path;
			return true;
		}
		return false;
	}

	void Pipe::Close()
	{
		if (m_State->m_Socket >= 0)
		{
			close(m_State->m_Socket);
		}
		m_State->m_Socket = -1;
	}

	bool Pipe::IsOpen() const
	{
		return m_State->m_Socket >= 0;
	}

	// MSG_NOSIGNAL: a send to a closed peer must never raise SIGPIPE.
	WriteResult Pipe::Write(const std::string& frame)
	{
		if (m_State->m_Socket < 0)
		{
			return WriteResult::Failed;
		}

		const ssize_t sent = send(m_State->m_Socket, frame.data(), frame.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
		if (sent == static_cast<ssize_t>(frame.size()))
		{
			return WriteResult::Sent;
		}
		if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		{
			return WriteResult::Dropped;
		}
		// An error, or half a frame: the stream's framing is broken.
		return WriteResult::Failed;
	}

	// A send takes the whole frame or nothing, so nothing is ever in flight.
	void Pipe::Flush(int)
	{
	}

	bool Pipe::ReadAvailable(std::string& into)
	{
		if (m_State->m_Socket < 0)
		{
			return false;
		}

		char buffer[4096];
		size_t total = 0;
		while (total < READ_MAX)
		{
			const ssize_t got = recv(m_State->m_Socket, buffer, sizeof(buffer), MSG_DONTWAIT);
			if (got > 0)
			{
				into.append(buffer, static_cast<size_t>(got));
				total += static_cast<size_t>(got);
				continue;
			}
			if (got == 0)
			{
				return false;
			}
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
		}
		return true;
	}

	uint32_t Pipe::ProcessId()
	{
		return static_cast<uint32_t>(getpid());
	}

}
