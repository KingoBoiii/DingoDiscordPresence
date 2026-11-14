#include "TestFramework.h"

#include "Frame.h"

using namespace Dingo::Nametag::Internal;

TEST(Frame_EncodeWritesALittleEndianHeader)
{
	const std::string frame = EncodeFrame(1, "hi");
	CHECK(frame == std::string("\x01\0\0\0\x02\0\0\0hi", 10));

	const std::string big = EncodeFrame(0x04030201u, std::string(0x102, 'x'));
	CHECK(big.size() == 8 + 0x102);
	CHECK(big.substr(0, 8) == std::string("\x01\x02\x03\x04\x02\x01\0\0", 8));
}

TEST(Frame_EncodeAnEmptyBody)
{
	CHECK(EncodeFrame(OP_PONG, "") == std::string("\x04\0\0\0\0\0\0\0", 8));
}

TEST(Frame_DecodeAWholeFrame)
{
	std::string inbound = EncodeFrame(OP_PING, "{\"n\":1}");
	Frame frame;
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Frame);
	CHECK(frame.Opcode == OP_PING);
	CHECK(frame.Json == "{\"n\":1}");
	CHECK(inbound.empty());
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Incomplete);
}

TEST(Frame_DecodeASplitFrame)
{
	const std::string whole = EncodeFrame(OP_FRAME, "{\"evt\":\"READY\"}");
	std::string inbound = whole.substr(0, 8);
	Frame frame;
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Incomplete);
	CHECK(inbound.size() == 8);

	inbound += whole.substr(8, 4);
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Incomplete);
	CHECK(inbound.size() == 12);

	inbound += whole.substr(12);
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Frame);
	CHECK(frame.Opcode == OP_FRAME);
	CHECK(frame.Json == "{\"evt\":\"READY\"}");
	CHECK(inbound.empty());
}

TEST(Frame_DecodeAShortHeaderWaits)
{
	std::string inbound("\x01\0\0", 3);
	Frame frame;
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Incomplete);
	CHECK(inbound.size() == 3);
}

TEST(Frame_DecodeTwoFramesInOneBuffer)
{
	std::string inbound = EncodeFrame(OP_FRAME, "one") + EncodeFrame(OP_CLOSE, "two") + std::string("\x03\0", 2);
	Frame frame;
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Frame);
	CHECK(frame.Opcode == OP_FRAME);
	CHECK(frame.Json == "one");
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Frame);
	CHECK(frame.Opcode == OP_CLOSE);
	CHECK(frame.Json == "two");
	CHECK(inbound.size() == 2);
	CHECK(DecodeFrame(inbound, frame) == DecodeResult::Incomplete);
}

TEST(Frame_DecodeReportsAnOversizeLength)
{
	std::string over;
	AppendUint32(over, OP_FRAME);
	AppendUint32(over, static_cast<uint32_t>(FRAME_MAX + 1));
	Frame frame;
	CHECK(DecodeFrame(over, frame) == DecodeResult::Oversize);

	std::string exact;
	AppendUint32(exact, OP_FRAME);
	AppendUint32(exact, static_cast<uint32_t>(FRAME_MAX));
	CHECK(DecodeFrame(exact, frame) == DecodeResult::Incomplete);
}
