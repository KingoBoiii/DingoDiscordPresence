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

		std::string OpName(uint32_t opcode)
		{
			switch (opcode)
			{
			case Internal::OP_FRAME: return "FRAME";
			case Internal::OP_CLOSE: return "CLOSE";
			case Internal::OP_PING: return "PING";
			case Internal::OP_PONG: return "PONG";
			default: return std::to_string(opcode);
			}
		}

	}

	struct Client::Impl
	{
		explicit Impl(const Config& config)
			: m_Config(config), m_Enabled(config.Enabled), m_DryRun(config.AppId.empty()), m_Gate(config.MinSendSeconds)
		{
			if (config.PipeIndex >= 0 && config.PipeIndex < PIPE_COUNT)
			{
				m_PipeIndex = config.PipeIndex;
			}

			if (m_DryRun)
			{
				Info("off (no app id)");
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
			if (m_DryRun)
			{
				if (m_Pending && m_Gate.CanSend(now))
				{
					SendActivity(now);
				}
				return;
			}

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
			if (!m_Pipe.IsOpen())
			{
				return;
			}

			SendQueued(now);
			// A dropped activity that SetActivity then took back leaves nothing to retry, so no Sent would ever stop the clock.
			if (!m_Pong && !m_ClearPending && !m_Pending)
			{
				m_DropSince.reset();
			}
			if (m_Pipe.IsOpen() && m_DropSince && now - *m_DropSince >= m_Config.WriteTimeoutSeconds)
			{
				Disconnect("write timed out");
			}
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

		// Past the send gate, and not a send for it: only an activity counts against Discord's rate limit.
		void ClearCard()
		{
			m_Pending = false;
			ForgetSent();
			m_ClearPending = false;
			if (m_DryRun)
			{
				if (m_Enabled)
				{
					LogDryRun(Internal::ClearJson(++m_Nonce, Internal::Pipe::ProcessId()));
				}
				return;
			}
			if (!IsConnected())
			{
				return;
			}
			m_ClearPending = true;
			SendClear();
		}

		void SetEnabled(bool on)
		{
			if (on == m_Enabled)
			{
				return;
			}

			if (!on)
			{
				Shutdown();
				m_Enabled = false;
				return;
			}
			m_Enabled = true;
			m_NextTry = 0.0;
			m_Pending = m_Activity.has_value();
		}

		// The clear may still be in flight, or dropped, behind a full pipe, and closing now would cancel it and leave the
		// card showing.
		void Shutdown()
		{
			ClearCard();
			m_Pipe.Flush(200);
			if (m_ClearPending)
			{
				SendClear();
			}
			Disconnect(nullptr);
		}

		// Discord shows nothing of ours any more: after a clear, or because the pipe closed.
		void ForgetSent()
		{
			m_Sent.reset();
			if (!m_Config.KeepSendGapAcrossReconnects)
			{
				m_Gate.Reset();
			}
		}

		bool IsConnected() const
		{
			return m_Pipe.IsOpen() && m_Ready;
		}

		// Nothing more is written until a fresh connect, handshake and READY. What was queued is dropped with the pipe:
		// Discord takes the card down when the pipe closes.
		void Disconnect(const char* why)
		{
			if (!m_Pipe.IsOpen())
			{
				return;
			}

			m_Pipe.Close();
			m_Ready = false;
			ForgetSent();
			m_ClearPending = false;
			m_Pong.reset();
			m_DropSince.reset();
			m_Inbound.clear();
			m_NextTry = NowSeconds() + m_Config.RetrySeconds;
			if (why)
			{
				Warn("lost ({})", why);
			}
		}

		// A failed write is Discord gone: the link is dropped and retried later. A dropped one (a full pipe) leaves the
		// connection as it is, and starts the clock of the write timeout.
		Internal::WriteResult WriteFrame(uint32_t opcode, const char* name, std::string_view json)
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
			else if (result == Internal::WriteResult::Dropped)
			{
				if (!m_DropSince)
				{
					m_DropSince = NowSeconds();
				}
			}
			else
			{
				m_DropSince.reset();
				if (m_Config.LogFrames)
				{
					LogFrame(std::string("send ") + name, opcode == Internal::OP_PONG ? std::string_view() : json);
				}
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
				if (WriteFrame(Internal::OP_HANDSHAKE, "HANDSHAKE", Internal::HandshakeJson(m_Config.AppId))
					== Internal::WriteResult::Dropped)
				{
					Disconnect("handshake dropped");
				}
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

				if (m_Config.LogFrames)
				{
					LogReceived(frame);
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
					m_Pong = std::move(frame.Json);
					SendPong();
					if (!m_Pipe.IsOpen())
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

		// What a write that was dropped left behind goes first, in this order: the pong, the clear, then the activity. A
		// dropped one stops the rest, which would be dropped as well.
		void SendQueued(double now)
		{
			if (!SendPong() || !m_Ready || !SendClear())
			{
				return;
			}
			if (m_Pending && m_Gate.CanSend(now))
			{
				SendActivity(now);
			}
		}

		// True when nothing is left to send. Only the latest pong is kept.
		bool SendPong()
		{
			if (!m_Pong)
			{
				return true;
			}

			const std::string body = *m_Pong;
			if (WriteFrame(Internal::OP_PONG, "PONG", body) != Internal::WriteResult::Sent)
			{
				return false;
			}
			m_Pong.reset();
			return true;
		}

		bool SendClear()
		{
			if (!m_ClearPending)
			{
				return true;
			}

			if (WriteFrame(Internal::OP_FRAME, "SET_ACTIVITY", Internal::ClearJson(++m_Nonce, Internal::Pipe::ProcessId()))
				!= Internal::WriteResult::Sent)
			{
				return false;
			}
			m_ClearPending = false;
			Info("cleared");
			return true;
		}

		void SendActivity(double now)
		{
			if (!m_Activity)
			{
				m_Pending = false;
				return;
			}

			const std::string json = Internal::ActivityJson(*m_Activity, ++m_Nonce, Internal::Pipe::ProcessId());
			if (m_DryRun)
			{
				LogDryRun(json);
			}
			else if (WriteFrame(Internal::OP_FRAME, "SET_ACTIVITY", json) != Internal::WriteResult::Sent)
			{
				return;
			}

			m_Pending = false;
			m_Sent = m_Activity;
			m_Gate.Sent(now);
			Info("activity \"{}\" / \"{}\"", m_Activity->Details, m_Activity->State);
		}

		void LogFrame(const std::string& what, std::string_view json) const
		{
			std::string line = what;
			if (!json.empty())
			{
				line += ' ';
				line += json;
			}
			Info("{}", line);
		}

		void LogDryRun(const std::string& json) const
		{
			if (m_Config.LogFrames)
			{
				LogFrame("send (dry run) SET_ACTIVITY", json);
			}
		}

		// Only the opcode and the frame's event or command name: the body is Discord's, and READY's holds the player's
		// user id, name and avatar.
		void LogReceived(const Internal::Frame& frame) const
		{
			LogFrame("recv " + OpName(frame.Opcode), Internal::FrameName(frame.Json));
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
		bool m_DryRun;
		int m_PipeIndex = -1;
		Internal::Pipe m_Pipe;
		Internal::SendGate m_Gate;
		double m_NextTry = 0.0;
		bool m_Ready = false;
		bool m_NotRunningLogged = false;
		std::optional<Activity> m_Activity;
		bool m_Pending = false;
		std::optional<Activity> m_Sent;
		bool m_ClearPending = false;
		std::optional<std::string> m_Pong;
		std::optional<double> m_DropSince;
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
