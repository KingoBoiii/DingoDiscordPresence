#pragma once

#include "DingoDiscordPresence/DiscordPresence.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace Dingo::DiscordPresence::Internal
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

	// The longest prefix of `utf8` that is at most `maxUtf16Units` long in UTF-16, Discord's unit, and never ends inside
	// a character. A byte that is not part of a whole sequence counts as one unit and passes through.
	inline std::string_view Truncated(std::string_view utf8, size_t maxUtf16Units)
	{
		size_t offset = 0;
		size_t units = 0;
		while (offset < utf8.size())
		{
			const unsigned char lead = static_cast<unsigned char>(utf8[offset]);
			size_t length = 1;
			size_t cost = 1;
			if (lead >= 0xC2 && lead <= 0xDF)
			{
				length = 2;
			}
			else if (lead >= 0xE0 && lead <= 0xEF)
			{
				length = 3;
			}
			else if (lead >= 0xF0 && lead <= 0xF4)
			{
				length = 4;
				cost = 2;
			}

			bool whole = offset + length <= utf8.size();
			for (size_t i = 1; whole && i < length; i++)
			{
				whole = (static_cast<unsigned char>(utf8[offset + i]) & 0xC0) == 0x80;
			}
			if (!whole)
			{
				length = 1;
				cost = 1;
			}

			if (units + cost > maxUtf16Units)
			{
				break;
			}
			units += cost;
			offset += length;
		}
		return utf8.substr(0, offset);
	}

	enum class JsonField
	{
		Missing,
		Null,
		Text,
		Other
	};

	// A string search, not a parser: the first "key" that is followed by a colon.
	inline JsonField FindField(std::string_view json, std::string_view key, std::string_view& text)
	{
		const auto skipSpace = [&json](size_t at)
		{
			while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n'))
			{
				at++;
			}
			return at;
		};

		const std::string needle = "\"" + std::string(key) + "\"";
		for (size_t from = json.find(needle); from != std::string_view::npos; from = json.find(needle, from + 1))
		{
			size_t at = skipSpace(from + needle.size());
			if (at >= json.size() || json[at] != ':')
			{
				continue;
			}

			at = skipSpace(at + 1);
			if (json.substr(at, 4) == "null")
			{
				return JsonField::Null;
			}
			if (at < json.size() && json[at] == '"')
			{
				const size_t end = json.find('"', at + 1);
				if (end == std::string_view::npos)
				{
					return JsonField::Other;
				}
				text = json.substr(at + 1, end - at - 1);
				return JsonField::Text;
			}
			return JsonField::Other;
		}
		return JsonField::Missing;
	}

	inline bool IsFrameName(std::string_view name)
	{
		if (name.empty() || name.size() > 32)
		{
			return false;
		}
		for (const char c : name)
		{
			if ((c < 'A' || c > 'Z') && c != '_')
			{
				return false;
			}
		}
		return true;
	}

	// The event name of a frame Discord sent, else its command name, or an empty string. This is the only part of a
	// received frame that may reach the log: it is a fixed word of 1 to 32 capital letters and underscores, so none of the
	// player's data can pass through it.
	inline std::string FrameName(std::string_view json)
	{
		std::string_view text;
		JsonField field = FindField(json, "evt", text);
		if (field == JsonField::Missing || field == JsonField::Null)
		{
			field = FindField(json, "cmd", text);
		}
		if (field != JsonField::Text || !IsFrameName(text))
		{
			return std::string();
		}
		return std::string(text);
	}

	inline std::string HandshakeJson(std::string_view appId)
	{
		return "{\"v\":1,\"client_id\":" + Quoted(appId) + "}";
	}

	constexpr size_t TEXT_MAX_UNITS = 128;
	constexpr size_t BUTTON_LABEL_MAX_UNITS = 32;
	constexpr size_t BUTTON_MAX = 2;

	// Parts with nothing in them are left out. Details is always written.
	inline std::string ActivityJson(const Activity& activity, uint32_t nonce, uint32_t pid)
	{
		std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + std::to_string(nonce) + "\",\"args\":{\"pid\":"
			+ std::to_string(pid) + ",\"activity\":{\"details\":" + Quoted(Truncated(activity.Details, TEXT_MAX_UNITS));
		if (!activity.State.empty())
		{
			json += ",\"state\":" + Quoted(Truncated(activity.State, TEXT_MAX_UNITS));
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
		if (!activity.LargeImage.empty() || !activity.SmallImage.empty())
		{
			json += ",\"assets\":{";
			bool first = true;
			const auto add = [&json, &first](const char* key, std::string_view value)
			{
				json += first ? "\"" : ",\"";
				json += key;
				json += "\":" + Quoted(value);
				first = false;
			};
			if (!activity.LargeImage.empty())
			{
				add("large_image", activity.LargeImage);
				if (!activity.LargeText.empty())
				{
					add("large_text", Truncated(activity.LargeText, TEXT_MAX_UNITS));
				}
			}
			if (!activity.SmallImage.empty())
			{
				add("small_image", activity.SmallImage);
				if (!activity.SmallText.empty())
				{
					add("small_text", Truncated(activity.SmallText, TEXT_MAX_UNITS));
				}
			}
			json += "}";
		}

		size_t buttons = 0;
		for (const Button& button : activity.Buttons)
		{
			if (button.Label.empty() || button.Url.empty())
			{
				continue;
			}
			json += buttons == 0 ? ",\"buttons\":[" : ",";
			json += "{\"label\":" + Quoted(Truncated(button.Label, BUTTON_LABEL_MAX_UNITS)) + ",\"url\":" + Quoted(button.Url) + "}";
			if (++buttons == BUTTON_MAX)
			{
				break;
			}
		}
		if (buttons > 0)
		{
			json += "]";
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
