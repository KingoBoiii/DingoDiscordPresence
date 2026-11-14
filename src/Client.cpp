#include "DingoNametag/Nametag.h"

#include "Frame.h"
#include "Json.h"
#include "Platform/Pipe.h"
#include "SendGate.h"

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace Dingo::Nametag
{

	namespace
	{

		using Clock = std::chrono::steady_clock;

		// discord-ipc-0 to -9: Discord takes the lowest free one.
		constexpr int PIPE_COUNT = 10;

		double NowSeconds()
		{
			return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
		}

	}

	struct Client::Impl
	{
		explicit Impl(const Config& config)
			: m_Config(config), m_Enabled(config.Enabled), m_Gate(config.MinSendSeconds)
		{
			if (config.PipeIndex >= 0 && config.PipeIndex < PIPE_COUNT)
			{
				m_PipeIndex = config.PipeIndex;
			}

			if (m_Config.AppId.empty())
			{
				Warn("no app id");
				m_Enabled = false;
				m_NoAppId = true;
				return;
			}

			Info("presence {}{}", m_Enabled ? "on" : "off (setting)",
				m_PipeIndex >= 0 ? " | pipe pinned to discord-ipc-" + std::to_string(m_PipeIndex) : std::string());
		}

		void Update()
		{
			if (!m_Enabled)
			{
				return;
			}

			const double now = NowSeconds();
			if (!m_Pipe.IsOpen())
			{
				if (now < m_NextTry)
				{
					return;
				}
				Connect(now);
				if (!m_Pipe.IsOpen())
				{
					return;
				}
			}

			Drain();
			if (!m_Pipe.IsOpen() || !m_Ready || !m_Pending)
			{
				return;
			}
			if (!m_Gate.CanSend(now))
			{
				return;
			}
			SendActivity(now);
		}

		void SetActivity(const Activity& activity)
		{
			if (m_Activity && *m_Activity == activity)
			{
				return;
			}
			m_Activity = activity;
			// Back to what Discord already shows before Update ran: nothing to send, and the rate limit keeps its frame.
			m_Pending = !(m_Sent && *m_Sent == activity);
		}

		// Forgets the activity too: SetActivity with the value shown before the clear must send again.
		void Clear()
		{
			ClearCard();
			m_Activity.reset();
		}

		void ClearCard()
		{
			m_Pending = false;
			ForgetSent();
			if (!m_Pipe.IsOpen() || !m_Ready)
			{
				return;
			}
			if (WriteFrame(Internal::OP_FRAME, Internal::ClearJson(++m_Nonce, Internal::Pipe::ProcessId())) == Internal::WriteResult::Sent)
			{
				Info("cleared");
			}
		}

		void SetEnabled(bool on)
		{
			if (m_NoAppId || on == m_Enabled)
			{
				return;
			}

			m_Enabled = on;
			if (!on)
			{
				Shutdown();
				return;
			}
			m_NextTry = 0.0;
			m_Pending = m_Activity.has_value();
		}

		// The clear may still be in flight behind a full pipe, and closing now would cancel it and leave the card showing.
		void Shutdown()
		{
			ClearCard();
			m_Pipe.Flush(200);
			Disconnect(nullptr);
		}

		bool IsConnected() const
		{
			return m_Pipe.IsOpen() && m_Ready;
		}

		void ForgetSent()
		{
			m_Sent.reset();
			m_Gate.Reset();
		}

		// Nothing more is written until a fresh connect, handshake and READY.
		void Disconnect(const char* why)
		{
			if (!m_Pipe.IsOpen())
			{
				return;
			}

			m_Pipe.Close();
			m_Ready = false;
			ForgetSent();
			m_Inbound.clear();
			m_NextTry = NowSeconds() + m_Config.RetrySeconds;
			if (why)
			{
				Warn("lost ({})", why);
			}
		}

		// A failed write is Discord gone: the link is dropped and retried later. A dropped one (a full pipe) leaves the
		// connection as it is.
		Internal::WriteResult WriteFrame(uint32_t opcode, std::string_view json)
		{
			if (!m_Pipe.IsOpen())
			{
				return Internal::WriteResult::Failed;
			}

			const Internal::WriteResult result = m_Pipe.Write(Internal::EncodeFrame(opcode, json));
			if (result == Internal::WriteResult::Failed)
			{
				Disconnect("write failed");
			}
			return result;
		}

		void Connect(double now)
		{
			m_NextTry = now + m_Config.RetrySeconds;
			const int first = m_PipeIndex >= 0 ? m_PipeIndex : 0;
			const int last = m_PipeIndex >= 0 ? m_PipeIndex : PIPE_COUNT - 1;
			for (int index = first; index <= last; index++)
			{
				std::string where;
				if (!m_Pipe.Open(index, where))
				{
					continue;
				}

				Info("connected ({})", where);
				m_Ready = false;
				m_Inbound.clear();
				ForgetSent();
				m_Pending = m_Activity.has_value();
				WriteFrame(Internal::OP_HANDSHAKE, Internal::HandshakeJson(m_Config.AppId));
				return;
			}

			if (!m_NotRunningLogged)
			{
				Info("not running");
				m_NotRunningLogged = true;
			}
		}

		// READY flips the ready flag, a PING gets its PONG, CLOSE drops the link, and the rest is dropped unread: left in
		// the pipe, the answers to every frame would fill it in a long session. Each frame leaves m_Inbound before it is
		// acted on, because a failed PONG clears m_Inbound under us.
		void Drain()
		{
			if (!m_Pipe.ReadAvailable(m_Inbound))
			{
				Disconnect("read failed");
				return;
			}

			Internal::Frame frame;
			while (true)
			{
				const Internal::DecodeResult result = Internal::DecodeFrame(m_Inbound, frame);
				if (result == Internal::DecodeResult::Incomplete)
				{
					return;
				}
				if (result == Internal::DecodeResult::Oversize)
				{
					Disconnect("bad frame");
					return;
				}

				if (frame.Opcode == Internal::OP_FRAME)
				{
					if (!m_Ready && frame.Json.find("\"evt\":\"READY\"") != std::string::npos)
					{
						m_Ready = true;
						Info("ready");
					}
				}
				else if (frame.Opcode == Internal::OP_PING)
				{
					if (WriteFrame(Internal::OP_PONG, frame.Json) == Internal::WriteResult::Failed)
					{
						return;
					}
				}
				else if (frame.Opcode == Internal::OP_CLOSE)
				{
					Disconnect("closed by discord");
					return;
				}
			}
		}

		void SendActivity(double now)
		{
			if (!m_Activity)
			{
				m_Pending = false;
				return;
			}

			const std::string json = Internal::ActivityJson(*m_Activity, ++m_Nonce, Internal::Pipe::ProcessId());
			if (WriteFrame(Internal::OP_FRAME, json) != Internal::WriteResult::Sent)
			{
				return;
			}

			m_Pending = false;
			m_Sent = m_Activity;
			m_Gate.Sent(now);
			Info("activity \"{}\" / \"{}\"", m_Activity->Details, m_Activity->State);
		}

		template<typename... Args>
		void Info(std::format_string<Args...> format, Args&&... args) const
		{
			if (m_Config.Log)
			{
				m_Config.Log(LogLevel::Info, std::format(format, std::forward<Args>(args)...), m_Config.LogUser);
			}
		}

		template<typename... Args>
		void Warn(std::format_string<Args...> format, Args&&... args) const
		{
			if (m_Config.Log)
			{
				m_Config.Log(LogLevel::Warn, std::format(format, std::forward<Args>(args)...), m_Config.LogUser);
			}
		}

		Config m_Config;
		bool m_Enabled;
		bool m_NoAppId = false;
		int m_PipeIndex = -1;
		Internal::Pipe m_Pipe;
		Internal::SendGate m_Gate;
		double m_NextTry = 0.0;
		bool m_Ready = false;
		bool m_NotRunningLogged = false;
		std::optional<Activity> m_Activity;
		bool m_Pending = false;
		std::optional<Activity> m_Sent;
		uint32_t m_Nonce = 0;
		std::string m_Inbound;
	};

	Client::Client(const Config& config)
		: m_Impl(std::make_unique<Impl>(config))
	{
	}

	Client::~Client()
	{
		m_Impl->Shutdown();
	}

	void Client::Update()
	{
		m_Impl->Update();
	}

	void Client::SetActivity(const Activity& activity)
	{
		m_Impl->SetActivity(activity);
	}

	void Client::Clear()
	{
		m_Impl->Clear();
	}

	void Client::SetEnabled(bool on)
	{
		m_Impl->SetEnabled(on);
	}

	bool Client::IsConnected() const
	{
		return m_Impl->IsConnected();
	}

}
