#include "FakeDiscord.h"

std::string FakeDiscord::Encode(uint32_t opcode, std::string_view json)
{
	std::string frame;
	const uint32_t length = static_cast<uint32_t>(json.size());
	for (int shift = 0; shift < 32; shift += 8)
	{
		frame += static_cast<char>((opcode >> shift) & 0xFFu);
	}
	for (int shift = 0; shift < 32; shift += 8)
	{
		frame += static_cast<char>((length >> shift) & 0xFFu);
	}
	frame.append(json);
	return frame;
}

std::string FakeDiscord::ReadyFrame()
{
	return Encode(1, std::string("{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1,\"config\":{},\"user\":{\"id\":\"1\",\"username\":\"")
		+ USER_NAME + "\"}},\"nonce\":null}");
}

void FakeDiscord::Pump()
{
	if (!m_Attached)
	{
		if (!Accept())
		{
			return;
		}
		m_Attached = true;
		m_Connections++;
		m_Inbound.clear();
	}

	if (!m_Reading)
	{
		return;
	}

	std::string bytes;
	const bool alive = ReadAll(bytes);
	m_Inbound += bytes;
	ParseFrames();
	if (!alive)
	{
		Detach();
		m_Attached = false;
	}
}

void FakeDiscord::ParseFrames()
{
	while (m_Inbound.size() >= 8)
	{
		uint32_t opcode = 0;
		uint32_t length = 0;
		for (size_t i = 0; i < 4; i++)
		{
			opcode |= static_cast<uint32_t>(static_cast<unsigned char>(m_Inbound[i])) << (8 * i);
			length |= static_cast<uint32_t>(static_cast<unsigned char>(m_Inbound[4 + i])) << (8 * i);
		}
		if (m_Inbound.size() < 8 + static_cast<size_t>(length))
		{
			return;
		}

		m_Frames.push_back({ opcode, m_Inbound.substr(8, length) });
		m_Inbound.erase(0, 8 + static_cast<size_t>(length));
		if (opcode == 0 && m_AutoReady)
		{
			SendReady();
		}
	}
}

int FakeDiscord::CountOpcode(uint32_t opcode) const
{
	int count = 0;
	for (const Received& frame : m_Frames)
	{
		if (frame.Opcode == opcode)
		{
			count++;
		}
	}
	return count;
}

int FakeDiscord::CountFramesWith(std::string_view text) const
{
	int count = 0;
	for (const Received& frame : m_Frames)
	{
		if (frame.Opcode == 1 && frame.Json.find(text) != std::string::npos)
		{
			count++;
		}
	}
	return count;
}

void FakeDiscord::SendReady()
{
	SendRaw(ReadyFrame());
}

void FakeDiscord::SendFrame(uint32_t opcode, std::string_view json)
{
	SendRaw(Encode(opcode, json));
}

void FakeDiscord::SendPing(std::string_view body)
{
	SendFrame(3, body);
}

void FakeDiscord::SendClose()
{
	SendFrame(2, "{\"code\":1000,\"message\":\"bye\"}");
}

void FakeDiscord::SendRaw(std::string_view bytes)
{
	if (m_Attached)
	{
		WriteAll(std::string(bytes));
	}
}

void FakeDiscord::DropClient()
{
	if (m_Attached)
	{
		Detach();
		m_Attached = false;
	}
}
