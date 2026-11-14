#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Dingo::Nametag::Internal
{

	constexpr uint32_t OP_HANDSHAKE = 0;
	constexpr uint32_t OP_FRAME = 1;
	constexpr uint32_t OP_CLOSE = 2;
	constexpr uint32_t OP_PING = 3;
	constexpr uint32_t OP_PONG = 4;

	constexpr size_t FRAME_HEADER_SIZE = 8;
	// A reply longer than this is not Discord: the link is dropped.
	constexpr size_t FRAME_MAX = 1u << 20;

	enum class DecodeResult
	{
		Frame,
		Incomplete,
		Oversize
	};

	struct Frame
	{
		uint32_t Opcode = 0;
		std::string Json;
	};

	inline void AppendUint32(std::string& out, uint32_t value)
	{
		for (int shift = 0; shift < 32; shift += 8)
		{
			out += static_cast<char>((value >> shift) & 0xFFu);
		}
	}

	inline uint32_t ReadUint32(const std::string& in, size_t offset)
	{
		uint32_t value = 0;
		for (size_t i = 0; i < 4; i++)
		{
			value |= static_cast<uint32_t>(static_cast<unsigned char>(in[offset + i])) << (8 * i);
		}
		return value;
	}

	inline std::string EncodeFrame(uint32_t opcode, std::string_view json)
	{
		std::string frame;
		frame.reserve(FRAME_HEADER_SIZE + json.size());
		AppendUint32(frame, opcode);
		AppendUint32(frame, static_cast<uint32_t>(json.size()));
		frame.append(json);
		return frame;
	}

	// A whole frame is removed from `inbound` before it is returned, so the caller may act on it however it likes.
	// Incomplete leaves `inbound` as it is: the rest comes with the next read.
	inline DecodeResult DecodeFrame(std::string& inbound, Frame& frame)
	{
		if (inbound.size() < FRAME_HEADER_SIZE)
		{
			return DecodeResult::Incomplete;
		}

		const uint32_t length = ReadUint32(inbound, 4);
		if (length > FRAME_MAX)
		{
			return DecodeResult::Oversize;
		}
		if (inbound.size() < FRAME_HEADER_SIZE + static_cast<size_t>(length))
		{
			return DecodeResult::Incomplete;
		}

		frame.Opcode = ReadUint32(inbound, 0);
		frame.Json = inbound.substr(FRAME_HEADER_SIZE, length);
		inbound.erase(0, FRAME_HEADER_SIZE + static_cast<size_t>(length));
		return DecodeResult::Frame;
	}

}
