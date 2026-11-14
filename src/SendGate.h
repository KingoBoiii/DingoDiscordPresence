#pragma once

namespace Dingo::DiscordPresence::Internal
{

	// Discord's rate limit: the first activity goes at once, then at most one per gap. Time is passed in, in
	// seconds, so the gate never reads a clock.
	class SendGate
	{
	public:
		explicit SendGate(double minSeconds)
			: m_MinSeconds(minSeconds)
		{
		}

		bool CanSend(double now) const
		{
			return !m_HaveSent || now - m_LastSend >= m_MinSeconds;
		}

		void Sent(double now)
		{
			m_HaveSent = true;
			m_LastSend = now;
		}

		void Reset()
		{
			m_HaveSent = false;
		}

	private:
		double m_MinSeconds;
		double m_LastSend = 0.0;
		bool m_HaveSent = false;
	};

}
