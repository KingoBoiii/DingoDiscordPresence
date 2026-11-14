#ifdef DDP_PLATFORM_WINDOWS

#include "FakeDiscord.h"

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <Windows.h>

namespace
{

	// The client's side of the pipe holds only this much unread, so a Discord that stops reading fills it after a frame or two.
	constexpr DWORD IN_BUFFER = 1024;
	constexpr DWORD OUT_BUFFER = 1u << 16;
	constexpr DWORD IO_WAIT_MS = 50;

}

struct FakeDiscord::Transport
{
	HANDLE m_Pipe = INVALID_HANDLE_VALUE;
	HANDLE m_ConnectEvent = nullptr;
	HANDLE m_IoEvent = nullptr;
	OVERLAPPED m_Connect{};
	bool m_ConnectPending = false;
	bool m_Connected = false;

	void Listen()
	{
		m_Connected = false;
		m_ConnectPending = false;
		m_Connect = {};
		m_Connect.hEvent = m_ConnectEvent;
		ResetEvent(m_ConnectEvent);
		if (ConnectNamedPipe(m_Pipe, &m_Connect))
		{
			m_Connected = true;
			return;
		}
		const DWORD error = GetLastError();
		if (error == ERROR_IO_PENDING)
		{
			m_ConnectPending = true;
		}
		else if (error == ERROR_PIPE_CONNECTED)
		{
			m_Connected = true;
		}
	}
};

FakeDiscord::FakeDiscord()
	: m_Transport(std::make_unique<Transport>())
{
	Transport& transport = *m_Transport;
	transport.m_ConnectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	transport.m_IoEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

	const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(PIPE_INDEX);
	transport.m_Pipe = CreateNamedPipeW(name.c_str(),
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, OUT_BUFFER, IN_BUFFER, 0, nullptr);
	if (transport.m_Pipe != INVALID_HANDLE_VALUE)
	{
		transport.Listen();
	}
}

FakeDiscord::~FakeDiscord()
{
	Transport& transport = *m_Transport;
	if (transport.m_Pipe != INVALID_HANDLE_VALUE)
	{
		if (transport.m_ConnectPending)
		{
			CancelIoEx(transport.m_Pipe, &transport.m_Connect);
			DWORD done = 0;
			GetOverlappedResult(transport.m_Pipe, &transport.m_Connect, &done, TRUE);
		}
		CloseHandle(transport.m_Pipe);
	}
	if (transport.m_ConnectEvent)
	{
		CloseHandle(transport.m_ConnectEvent);
	}
	if (transport.m_IoEvent)
	{
		CloseHandle(transport.m_IoEvent);
	}
}

bool FakeDiscord::Ok() const
{
	return m_Transport->m_Pipe != INVALID_HANDLE_VALUE;
}

bool FakeDiscord::Accept()
{
	Transport& transport = *m_Transport;
	if (transport.m_Pipe == INVALID_HANDLE_VALUE)
	{
		return false;
	}
	if (transport.m_ConnectPending)
	{
		DWORD done = 0;
		if (GetOverlappedResult(transport.m_Pipe, &transport.m_Connect, &done, FALSE))
		{
			transport.m_ConnectPending = false;
			transport.m_Connected = true;
		}
	}
	return transport.m_Connected;
}

bool FakeDiscord::ReadAll(std::string& into)
{
	Transport& transport = *m_Transport;
	for (int round = 0; round < 64; round++)
	{
		DWORD available = 0;
		if (!PeekNamedPipe(transport.m_Pipe, nullptr, 0, nullptr, &available, nullptr))
		{
			return false;
		}
		if (available == 0)
		{
			return true;
		}

		const size_t had = into.size();
		into.resize(had + available);
		OVERLAPPED overlapped{};
		overlapped.hEvent = transport.m_IoEvent;
		ResetEvent(transport.m_IoEvent);
		if (!ReadFile(transport.m_Pipe, into.data() + had, available, nullptr, &overlapped)
			&& GetLastError() != ERROR_IO_PENDING)
		{
			into.resize(had);
			return false;
		}

		DWORD read = 0;
		if (!GetOverlappedResult(transport.m_Pipe, &overlapped, &read, TRUE))
		{
			into.resize(had);
			return false;
		}
		into.resize(had + read);
	}
	return true;
}

bool FakeDiscord::WriteAll(const std::string& bytes)
{
	Transport& transport = *m_Transport;
	OVERLAPPED overlapped{};
	overlapped.hEvent = transport.m_IoEvent;
	ResetEvent(transport.m_IoEvent);
	if (!WriteFile(transport.m_Pipe, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &overlapped))
	{
		if (GetLastError() != ERROR_IO_PENDING)
		{
			return false;
		}
		if (WaitForSingleObject(transport.m_IoEvent, IO_WAIT_MS) != WAIT_OBJECT_0)
		{
			CancelIoEx(transport.m_Pipe, &overlapped);
		}
	}

	DWORD written = 0;
	return GetOverlappedResult(transport.m_Pipe, &overlapped, &written, TRUE) && written == bytes.size();
}

void FakeDiscord::Detach()
{
	Transport& transport = *m_Transport;
	DisconnectNamedPipe(transport.m_Pipe);
	transport.Listen();
}

#endif
