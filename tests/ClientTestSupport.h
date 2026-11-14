#pragma once

#include "DingoNametag/Nametag.h"

#include <string>
#include <utility>
#include <vector>

struct LogRecorder
{
	std::vector<std::pair<Dingo::Nametag::LogLevel, std::string>> m_Lines;

	static void Callback(Dingo::Nametag::LogLevel level, std::string_view message, void* user)
	{
		static_cast<LogRecorder*>(user)->m_Lines.emplace_back(level, std::string(message));
	}

	int Count(const std::string& message) const
	{
		int count = 0;
		for (const auto& line : m_Lines)
		{
			if (line.second == message)
			{
				count++;
			}
		}
		return count;
	}

	bool Has(Dingo::Nametag::LogLevel level, const std::string& message) const
	{
		for (const auto& line : m_Lines)
		{
			if (line.first == level && line.second == message)
			{
				return true;
			}
		}
		return false;
	}

	bool HasLine(const std::string& message) const
	{
		return Count(message) > 0;
	}

	bool HasLineStartingWith(const std::string& prefix) const
	{
		for (const auto& line : m_Lines)
		{
			if (line.second.compare(0, prefix.size(), prefix) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool AnyLineContains(const std::string& text) const
	{
		for (const auto& line : m_Lines)
		{
			if (line.second.find(text) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}
};

inline Dingo::Nametag::Config MakeConfig(LogRecorder& log)
{
	Dingo::Nametag::Config config;
	config.AppId = "123456789012345678";
	config.PipeIndex = 9;
	config.RetrySeconds = 0.0;
	config.Log = &LogRecorder::Callback;
	config.LogUser = &log;
	return config;
}
