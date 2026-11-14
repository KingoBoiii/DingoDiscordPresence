#ifdef DDP_PLATFORM_LINUX

#include "FakeDiscord.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct FakeDiscord::Transport
{
	std::string m_Folder;
	std::string m_Path;
	std::string m_PreviousRuntimeDir;
	bool m_HadRuntimeDir = false;
	int m_Listener = -1;
	int m_Client = -1;
};

FakeDiscord::FakeDiscord()
	: m_Transport(std::make_unique<Transport>())
{
	Transport& transport = *m_Transport;

	// The library looks under XDG_RUNTIME_DIR first: pointing it at a folder of ours means no real Discord socket is reachable.
	char folder[] = "/tmp/dnt-fake-XXXXXX";
	if (!mkdtemp(folder))
	{
		return;
	}
	transport.m_Folder = folder;
	if (const char* previous = std::getenv("XDG_RUNTIME_DIR"))
	{
		transport.m_HadRuntimeDir = true;
		transport.m_PreviousRuntimeDir = previous;
	}
	setenv("XDG_RUNTIME_DIR", folder, 1);

	transport.m_Path = transport.m_Folder + "/discord-ipc-" + std::to_string(PIPE_INDEX);
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	if (transport.m_Path.size() >= sizeof(address.sun_path))
	{
		return;
	}
	std::memcpy(address.sun_path, transport.m_Path.c_str(), transport.m_Path.size() + 1);

	const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
	{
		return;
	}
	if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 4) != 0)
	{
		close(fd);
		return;
	}
	transport.m_Listener = fd;
}

FakeDiscord::~FakeDiscord()
{
	Transport& transport = *m_Transport;
	if (transport.m_Client >= 0)
	{
		close(transport.m_Client);
	}
	if (transport.m_Listener >= 0)
	{
		close(transport.m_Listener);
	}
	if (!transport.m_Folder.empty())
	{
		unlink(transport.m_Path.c_str());
		rmdir(transport.m_Folder.c_str());
		if (transport.m_HadRuntimeDir)
		{
			setenv("XDG_RUNTIME_DIR", transport.m_PreviousRuntimeDir.c_str(), 1);
		}
		else
		{
			unsetenv("XDG_RUNTIME_DIR");
		}
	}
}

bool FakeDiscord::Ok() const
{
	return m_Transport->m_Listener >= 0;
}

bool FakeDiscord::Accept()
{
	Transport& transport = *m_Transport;
	if (transport.m_Listener < 0)
	{
		return false;
	}
	const int fd = accept4(transport.m_Listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (fd < 0)
	{
		return false;
	}
	transport.m_Client = fd;
	return true;
}

bool FakeDiscord::ReadAll(std::string& into)
{
	char buffer[4096];
	while (true)
	{
		const ssize_t got = recv(m_Transport->m_Client, buffer, sizeof(buffer), MSG_DONTWAIT);
		if (got > 0)
		{
			into.append(buffer, static_cast<size_t>(got));
			continue;
		}
		if (got == 0)
		{
			return false;
		}
		return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
	}
}

bool FakeDiscord::WriteAll(const std::string& bytes)
{
	size_t done = 0;
	for (int waits = 0; done < bytes.size();)
	{
		const ssize_t sent = send(m_Transport->m_Client, bytes.data() + done, bytes.size() - done, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (sent > 0)
		{
			done += static_cast<size_t>(sent);
			continue;
		}
		if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && waits++ < 5)
		{
			pollfd wait{ m_Transport->m_Client, POLLOUT, 0 };
			poll(&wait, 1, 10);
			continue;
		}
		return false;
	}
	return true;
}

void FakeDiscord::Detach()
{
	Transport& transport = *m_Transport;
	if (transport.m_Client >= 0)
	{
		close(transport.m_Client);
	}
	transport.m_Client = -1;
}

#endif
