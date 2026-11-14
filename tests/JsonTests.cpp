#include "TestFramework.h"

#include "Json.h"

using namespace Dingo::Nametag;

TEST(Json_QuotedEscapesQuoteAndBackslash)
{
	CHECK(Internal::Quoted("say \"hi\"") == "\"say \\\"hi\\\"\"");
	CHECK(Internal::Quoted("a\\b") == "\"a\\\\b\"");
}

TEST(Json_QuotedEscapesWhitespaceAndControlCharacters)
{
	CHECK(Internal::Quoted("a\nb\r\tc") == "\"a\\nb\\r\\tc\"");
	CHECK(Internal::Quoted(std::string_view("a\x01" "b", 3)) == "\"a\\u0001b\"");
	CHECK(Internal::Quoted(std::string_view("\x1f", 1)) == "\"\\u001f\"");
}

TEST(Json_QuotedPassesUtf8Through)
{
	CHECK(Internal::Quoted("caf\xC3\xA9") == "\"caf\xC3\xA9\"");
	CHECK(Internal::Quoted("") == "\"\"");
}

TEST(Json_HandshakeQuotesTheAppId)
{
	CHECK(Internal::HandshakeJson("123456789012345678") == "{\"v\":1,\"client_id\":\"123456789012345678\"}");
	CHECK(Internal::HandshakeJson("a\"b") == "{\"v\":1,\"client_id\":\"a\\\"b\"}");
}

TEST(Json_ActivityWithOnlyDetails)
{
	Activity activity;
	activity.Details = "Wave 12";
	CHECK(Internal::ActivityJson(activity, 7, 42)
		== "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"7\",\"args\":{\"pid\":42,\"activity\":{\"details\":\"Wave 12\"}}}");
}

TEST(Json_ActivityWithEverything)
{
	Activity activity;
	activity.Details = "Wave 12";
	activity.State = "Graveyard";
	activity.StartUnix = 1700000000;
	activity.LargeImage = "headstone";
	activity.LargeText = "Headstone";
	activity.PartySize = 2;
	activity.PartyMax = 4;
	activity.PartyId = "lobby-9";
	CHECK(Internal::ActivityJson(activity, 1, 5)
		== "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"1\",\"args\":{\"pid\":5,\"activity\":{\"details\":\"Wave 12\","
			"\"state\":\"Graveyard\",\"timestamps\":{\"start\":1700000000},"
			"\"party\":{\"id\":\"lobby-9\",\"size\":[2,4]},"
			"\"assets\":{\"large_image\":\"headstone\",\"large_text\":\"Headstone\"}}}}");
}

TEST(Json_ActivityLeavesPartyOutWithoutAnId)
{
	Activity activity;
	activity.Details = "x";
	activity.PartySize = 2;
	activity.PartyMax = 4;
	CHECK(Internal::ActivityJson(activity, 1, 1).find("party") == std::string::npos);

	activity.PartyId = "p";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("\"party\":{\"id\":\"p\",\"size\":[2,4]}") != std::string::npos);

	activity.PartyMax = 0;
	CHECK(Internal::ActivityJson(activity, 1, 1).find("party") == std::string::npos);
}

TEST(Json_ActivityLeavesAssetsOutWithoutALargeImage)
{
	Activity activity;
	activity.Details = "x";
	activity.LargeText = "hover";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("assets") == std::string::npos);
	CHECK(Internal::ActivityJson(activity, 1, 1).find("large_text") == std::string::npos);

	activity.LargeImage = "art";
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("\"assets\":{\"large_image\":\"art\",\"large_text\":\"hover\"}") != std::string::npos);

	activity.LargeText.clear();
	CHECK(Internal::ActivityJson(activity, 1, 1).find("\"assets\":{\"large_image\":\"art\"}") != std::string::npos);
}

TEST(Json_ActivityEscapesItsText)
{
	Activity activity;
	activity.Details = "a\"b";
	activity.State = "c\\d";
	activity.LargeImage = "e\"f";
	activity.LargeText = "g\nh";
	activity.PartyId = "i\"j";
	activity.PartySize = 1;
	activity.PartyMax = 2;
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("\"details\":\"a\\\"b\"") != std::string::npos);
	CHECK(json.find("\"state\":\"c\\\\d\"") != std::string::npos);
	CHECK(json.find("\"large_image\":\"e\\\"f\"") != std::string::npos);
	CHECK(json.find("\"large_text\":\"g\\nh\"") != std::string::npos);
	CHECK(json.find("\"id\":\"i\\\"j\"") != std::string::npos);
}

TEST(Json_Clear)
{
	CHECK(Internal::ClearJson(3, 42) == "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"3\",\"args\":{\"pid\":42,\"activity\":null}}");
}
