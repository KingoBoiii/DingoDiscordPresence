#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A stand-in for the Discord client, for the tests only. It listens on discord-ipc-9, where a real Discord never is (it
// takes the lowest free index). On Linux it points XDG_RUNTIME_DIR at a folder of its own first, so no test can reach a
// real Discord socket. Nothing blocks and nothing runs on a thread: a test alternates Pump and Client::Update.
class FakeDiscord
{
public:
	struct Received
	{
		uint32_t Opcode = 0;
		std::string Json;
	};

	static constexpr int PIPE_INDEX = 9;
	// In the READY frame, to prove the library never logs what Discord sends.
	static constexpr const char* USER_NAME = "SECRET-USER";

	FakeDiscord();
	~FakeDiscord();

	FakeDiscord(const FakeDiscord&) = delete;
	FakeDiscord& operator=(const FakeDiscord&) = delete;

	// False when the pipe could not be created, for instance because another process holds discord-ipc-9.
	bool Ok() const;

	// Takes a waiting client, then reads what it has sent and answers the handshake.
	void Pump();

	bool HasClient() const
	{
		return m_Attached;
	}

	int Connections() const
	{
		return m_Connections;
	}

	const std::vector<Received>& Frames() const
	{
		return m_Frames;
	}

	int CountOpcode(uint32_t opcode) const;
	// Frames with opcode 1 whose JSON contains `text`.
	int CountFramesWith(std::string_view text) const;

	// On by default: the handshake is answered with READY at once.
	void SetAutoReady(bool on)
	{
		m_AutoReady = on;
	}

	// Off: nothing is read, so the client's pipe fills.
	void SetReading(bool on)
	{
		m_Reading = on;
	}

	static std::string Encode(uint32_t opcode, std::string_view json);
	static std::string ReadyFrame();

	void SendReady();
	void SendFrame(uint32_t opcode, std::string_view json);
	void SendPing(std::string_view body);
	void SendClose();
	void SendRaw(std::string_view bytes);
	// Closes the connection and listens again.
	void DropClient();

private:
	struct Transport;

	bool Accept();
	// False when the client is gone.
	bool ReadAll(std::string& into);
	bool WriteAll(const std::string& bytes);
	void Detach();
	void ParseFrames();

	std::unique_ptr<Transport> m_Transport;
	bool m_Attached = false;
	bool m_AutoReady = true;
	bool m_Reading = true;
	int m_Connections = 0;
	std::string m_Inbound;
	std::vector<Received> m_Frames;
};
