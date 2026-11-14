#include "TestFramework.h"

#include "SendGate.h"

using Dingo::Nametag::Internal::SendGate;

TEST(SendGate_TheFirstSendGoesAtOnce)
{
	SendGate gate(15.0);
	CHECK(gate.CanSend(0.0));
	CHECK(gate.CanSend(1234.5));
}

TEST(SendGate_TheSecondWaitsForTheGap)
{
	SendGate gate(15.0);
	gate.Sent(100.0);
	CHECK(!gate.CanSend(100.0));
	CHECK(!gate.CanSend(114.9));
	CHECK(gate.CanSend(115.0));
	CHECK(gate.CanSend(200.0));

	gate.Sent(115.0);
	CHECK(!gate.CanSend(120.0));
	CHECK(gate.CanSend(130.0));
}

TEST(SendGate_ResetLetsTheNextSendGoAtOnce)
{
	SendGate gate(15.0);
	gate.Sent(100.0);
	CHECK(!gate.CanSend(101.0));
	gate.Reset();
	CHECK(gate.CanSend(101.0));
}

TEST(SendGate_AZeroGapNeverBlocks)
{
	SendGate gate(0.0);
	gate.Sent(5.0);
	CHECK(gate.CanSend(5.0));
}
