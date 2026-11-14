#include "Platform/Pipe.h"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <Windows.h>

namespace Dingo::DiscordPresence::Internal
{

	namespace
	{

		constexpr DWORD READ_MAX = 1u << 16;

		void ResetOverlapped(OVERLAPPED& overlapped)
		{
			const HANDLE event = overlapped.hEvent;
			overlapped = {};
			overlapped.hEvent = event;
			ResetEvent(event);
		}

	}

	// An overlapped write owns its buffer and its OVERLAPPED until it completes, so both live here, never on the stack.
	struct Pipe::State
	{
		HANDLE m_Handle = INVALID_HANDLE_VALUE;
		OVERLAPPED m_WriteOverlapped{};
		OVERLAPPED m_ReadOverlapped{};
		std::string m_WriteBuffer;
		bool m_WritePending = false;
	};

	Pipe::Pipe()
		: m_State(std::make_unique<State>())
	{
	}

	Pipe::~Pipe()
	{
		Close();
		if (m_State->m_WriteOverlapped.hEvent)
		{
			CloseHandle(m_State->m_WriteOverlapped.hEvent);
		}
		if (m_State->m_ReadOverlapped.hEvent)
		{
			CloseHandle(m_State->m_ReadOverlapped.hEvent);
		}
	}

	bool Pipe::Open(int index, std::string& where)
	{
		State& state = *m_State;
		if (!state.m_WriteOverlapped.hEvent)
		{
			state.m_WriteOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		}
		if (!state.m_ReadOverlapped.hEvent)
		{
			state.m_ReadOverlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		}
		if (!state.m_WriteOverlapped.hEvent || !state.m_ReadOverlapped.hEvent)
		{
			return false;
		}

		// No WaitNamedPipe: a missing pipe fails with ERROR_FILE_NOT_FOUND at once, and a busy one (another client
		// mid-connect) is skipped the same way rather than waited for.
		const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(index);
		const HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		state.m_Handle = handle;
		state.m_WritePending = false;
		where = "discord-ipc-" + std::to_string(index);
		return true;
	}

	void Pipe::Close()
	{
		State& state = *m_State;
		if (state.m_Handle != INVALID_HANDLE_VALUE)
		{
			if (state.m_WritePending)
			{
				// The buffer must outlive the write: cancel it and let the cancel finish before the close.
				CancelIoEx(state.m_Handle, &state.m_WriteOverlapped);
				DWORD done = 0;
				GetOverlappedResult(state.m_Handle, &state.m_WriteOverlapped, &done, TRUE);
			}
			CloseHandle(state.m_Handle);
		}
		state.m_Handle = INVALID_HANDLE_VALUE;
		state.m_WritePending = false;
	}

	bool Pipe::IsOpen() const
	{
		return m_State->m_Handle != INVALID_HANDLE_VALUE;
	}

	WriteResult Pipe::Write(const std::string& frame)
	{
		State& state = *m_State;
		if (state.m_Handle == INVALID_HANDLE_VALUE)
		{
			return WriteResult::Failed;
		}

		if (state.m_WritePending)
		{
			DWORD done = 0;
			if (!GetOverlappedResult(state.m_Handle, &state.m_WriteOverlapped, &done, FALSE))
			{
				if (GetLastError() == ERROR_IO_INCOMPLETE)
				{
					return WriteResult::Dropped;
				}
				state.m_WritePending = false;
				return WriteResult::Failed;
			}
			state.m_WritePending = false;
		}

		state.m_WriteBuffer = frame;
		ResetOverlapped(state.m_WriteOverlapped);
		if (WriteFile(state.m_Handle, state.m_WriteBuffer.data(), static_cast<DWORD>(state.m_WriteBuffer.size()), nullptr,
			&state.m_WriteOverlapped))
		{
			return WriteResult::Sent;
		}
		if (GetLastError() != ERROR_IO_PENDING)
		{
			return WriteResult::Failed;
		}

		// Queued behind a full pipe: it goes out when Discord reads again. Nothing waits for it, and the next frame is
		// dropped while it is in flight.
		state.m_WritePending = true;
		return WriteResult::Sent;
	}

	void Pipe::Flush(int milliseconds)
	{
		State& state = *m_State;
		if (state.m_Handle == INVALID_HANDLE_VALUE || !state.m_WritePending)
		{
			return;
		}

		if (WaitForSingleObject(state.m_WriteOverlapped.hEvent, static_cast<DWORD>((std::max)(0, milliseconds))) == WAIT_OBJECT_0)
		{
			DWORD done = 0;
			GetOverlappedResult(state.m_Handle, &state.m_WriteOverlapped, &done, FALSE);
			state.m_WritePending = false;
		}
	}

	bool Pipe::ReadAvailable(std::string& into)
	{
		State& state = *m_State;
		if (state.m_Handle == INVALID_HANDLE_VALUE)
		{
			return false;
		}

		// PeekNamedPipe first, so ReadFile only asks for bytes that are already there.
		DWORD available = 0;
		if (!PeekNamedPipe(state.m_Handle, nullptr, 0, nullptr, &available, nullptr))
		{
			return false;
		}
		if (available == 0)
		{
			return true;
		}
		if (available > READ_MAX)
		{
			available = READ_MAX;
		}

		const size_t had = into.size();
		into.resize(had + available);
		ResetOverlapped(state.m_ReadOverlapped);
		if (!ReadFile(state.m_Handle, into.data() + had, available, nullptr, &state.m_ReadOverlapped))
		{
			if (GetLastError() != ERROR_IO_PENDING)
			{
				into.resize(had);
				return false;
			}
			// Peek said the bytes are there, so this completes at once; a bounded wait all the same.
			if (WaitForSingleObject(state.m_ReadOverlapped.hEvent, 50) != WAIT_OBJECT_0)
			{
				CancelIoEx(state.m_Handle, &state.m_ReadOverlapped);
			}
		}

		DWORD read = 0;
		if (!GetOverlappedResult(state.m_Handle, &state.m_ReadOverlapped, &read, TRUE))
		{
			into.resize(had);
			return GetLastError() == ERROR_OPERATION_ABORTED;
		}
		into.resize(had + read);
		return true;
	}

	uint32_t Pipe::ProcessId()
	{
		return static_cast<uint32_t>(GetCurrentProcessId());
	}

}
