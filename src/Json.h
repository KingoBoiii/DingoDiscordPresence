#pragma once

#include "DingoNametag/Nametag.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace Dingo::Nametag::Internal
{

	// A JSON string literal, quotes included. UTF-8 passes through: JSON takes it raw.
	inline std::string Quoted(std::string_view text)
	{
		std::string out;
		out.reserve(text.size() + 2);
		out += '"';
		for (const char c : text)
		{
			switch (c)
			{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
					out += buffer;
				}
				else
				{
					out += c;
				}
				break;
			}
		}
		out += '"';
		return out;
	}

	inline std::string HandshakeJson(std::string_view appId)
	{
		return "{\"v\":1,\"client_id\":" + Quoted(appId) + "}";
	}

	// Parts with nothing in them are left out. Details is always written.
	inline std::string ActivityJson(const Activity& activity, uint32_t nonce, uint32_t pid)
	{
		std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + std::to_string(nonce) + "\",\"args\":{\"pid\":"
			+ std::to_string(pid) + ",\"activity\":{\"details\":" + Quoted(activity.Details);
		if (!activity.State.empty())
		{
			json += ",\"state\":" + Quoted(activity.State);
		}
		if (activity.StartUnix > 0)
		{
			json += ",\"timestamps\":{\"start\":" + std::to_string(activity.StartUnix) + "}";
		}
		if (!activity.PartyId.empty() && activity.PartySize > 0 && activity.PartyMax > 0)
		{
			json += ",\"party\":{\"id\":" + Quoted(activity.PartyId) + ",\"size\":[" + std::to_string(activity.PartySize) + ","
				+ std::to_string(activity.PartyMax) + "]}";
		}
		if (!activity.LargeImage.empty())
		{
			json += ",\"assets\":{\"large_image\":" + Quoted(activity.LargeImage);
			if (!activity.LargeText.empty())
			{
				json += ",\"large_text\":" + Quoted(activity.LargeText);
			}
			json += "}";
		}
		json += "}}}";
		return json;
	}

	// "activity":null takes the card off the profile.
	inline std::string ClearJson(uint32_t nonce, uint32_t pid)
	{
		return "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + std::to_string(nonce) + "\",\"args\":{\"pid\":"
			+ std::to_string(pid) + ",\"activity\":null}}";
	}

}
