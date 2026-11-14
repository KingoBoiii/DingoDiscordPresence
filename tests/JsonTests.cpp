#include "TestFramework.h"

#include "Json.h"

using namespace Dingo::DiscordPresence;

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

TEST(Json_ActivityWithASmallImageOnly)
{
	Activity activity;
	activity.Details = "x";
	activity.SmallImage = "tiny";
	activity.SmallText = "hover";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("\"assets\":{\"small_image\":\"tiny\",\"small_text\":\"hover\"}")
		!= std::string::npos);

	activity.SmallText.clear();
	CHECK(Internal::ActivityJson(activity, 1, 1).find("\"assets\":{\"small_image\":\"tiny\"}") != std::string::npos);

	activity.SmallImage.clear();
	activity.SmallText = "orphan";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("assets") == std::string::npos);
	CHECK(Internal::ActivityJson(activity, 1, 1).find("small_text") == std::string::npos);
}

TEST(Json_ActivityWithBothImages)
{
	Activity activity;
	activity.Details = "x";
	activity.LargeImage = "big";
	activity.LargeText = "big hover";
	activity.SmallImage = "tiny";
	activity.SmallText = "tiny hover";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("\"assets\":{\"large_image\":\"big\",\"large_text\":\"big hover\","
		"\"small_image\":\"tiny\",\"small_text\":\"tiny hover\"}}") != std::string::npos);

	activity.LargeImage.clear();
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("large") == std::string::npos);
	CHECK(json.find("\"assets\":{\"small_image\":\"tiny\",\"small_text\":\"tiny hover\"}}") != std::string::npos);
}

TEST(Json_ActivityWithoutButtonsLeavesTheKeyOut)
{
	Activity activity;
	activity.Details = "x";
	CHECK(Internal::ActivityJson(activity, 1, 1).find("buttons") == std::string::npos);

	activity.Buttons = { { "", "https://a.example" }, { "Label", "" }, { "", "" } };
	CHECK(Internal::ActivityJson(activity, 1, 1).find("buttons") == std::string::npos);
}

TEST(Json_ActivityWithOneButton)
{
	Activity activity;
	activity.Details = "x";
	activity.Buttons = { { "Play", "https://a.example/play" } };
	CHECK(Internal::ActivityJson(activity, 1, 1)
		== "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"1\",\"args\":{\"pid\":1,\"activity\":{\"details\":\"x\","
			"\"buttons\":[{\"label\":\"Play\",\"url\":\"https://a.example/play\"}]}}}");
}

TEST(Json_ActivitySendsTheFirstTwoButtonsOnly)
{
	Activity activity;
	activity.Details = "x";
	activity.Buttons = { { "One", "https://1" }, { "Two", "https://2" }, { "Three", "https://3" } };
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("\"buttons\":[{\"label\":\"One\",\"url\":\"https://1\"},{\"label\":\"Two\",\"url\":\"https://2\"}]")
		!= std::string::npos);
	CHECK(json.find("Three") == std::string::npos);
}

TEST(Json_ActivitySkipsAButtonWithoutAUrl)
{
	Activity activity;
	activity.Details = "x";
	activity.Buttons = { { "One", "" }, { "Two", "https://2" }, { "Three", "https://3" }, { "Four", "https://4" } };
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("One") == std::string::npos);
	CHECK(json.find("\"buttons\":[{\"label\":\"Two\",\"url\":\"https://2\"},{\"label\":\"Three\",\"url\":\"https://3\"}]")
		!= std::string::npos);
	CHECK(json.find("Four") == std::string::npos);
}

TEST(Json_ActivityEscapesAButton)
{
	Activity activity;
	activity.Details = "x";
	activity.Buttons = { { "a\"b", "https://x/?q=\"1\"" } };
	CHECK(Internal::ActivityJson(activity, 1, 1).find("{\"label\":\"a\\\"b\",\"url\":\"https://x/?q=\\\"1\\\"\"}")
		!= std::string::npos);
}

TEST(Json_ActivityCutsItsTextAndNotItsUrl)
{
	Activity activity;
	activity.Details = std::string(200, 'd');
	activity.State = std::string(200, 's');
	activity.LargeImage = "big";
	activity.LargeText = std::string(200, 'l');
	activity.SmallImage = "tiny";
	activity.SmallText = std::string(200, 't');
	const std::string url = "https://x.example/" + std::string(100, 'u');
	activity.Buttons = { { std::string(50, 'b'), url } };
	const std::string json = Internal::ActivityJson(activity, 1, 1);
	CHECK(json.find("\"details\":\"" + std::string(128, 'd') + "\"") != std::string::npos);
	CHECK(json.find("\"state\":\"" + std::string(128, 's') + "\"") != std::string::npos);
	CHECK(json.find("\"large_text\":\"" + std::string(128, 'l') + "\"") != std::string::npos);
	CHECK(json.find("\"small_text\":\"" + std::string(128, 't') + "\"") != std::string::npos);
	CHECK(json.find("\"label\":\"" + std::string(32, 'b') + "\"") != std::string::npos);
	CHECK(json.find("\"url\":\"" + url + "\"") != std::string::npos);
	CHECK(json.find(std::string(129, 'd')) == std::string::npos);
	CHECK(json.find("assets\":{\"large_image\":\"big\"") != std::string::npos);
}

TEST(Json_TruncatedLeavesAShortTextAlone)
{
	CHECK(Internal::Truncated("", 128) == "");
	CHECK(Internal::Truncated("abc", 128) == "abc");
	CHECK(Internal::Truncated("abc", 0) == "");
}

TEST(Json_TruncatedKeepsAsciiAtTheLimit)
{
	const std::string exact(128, 'a');
	CHECK(Internal::Truncated(exact, 128) == exact);
	CHECK(Internal::Truncated(exact + "b", 128) == exact);
	CHECK(Internal::Truncated(exact + std::string(50, 'b'), 128).size() == 128);
}

TEST(Json_TruncatedNeverSplitsATwoByteCharacter)
{
	const std::string middleDot = "\xC2\xB7";
	const std::string ae = "\xC3\xA6";
	const std::string head(127, 'a');

	CHECK(Internal::Truncated(head + middleDot + "z", 128) == head + middleDot);
	CHECK(Internal::Truncated(head + ae + "z", 128) == head + ae);
	CHECK(Internal::Truncated(head + "a" + middleDot, 128) == head + "a");
	CHECK(Internal::Truncated(head + "a" + ae, 128) == head + "a");
	CHECK(Internal::Truncated(middleDot + middleDot + middleDot, 2) == middleDot + middleDot);
}

TEST(Json_TruncatedCountsAFourByteCharacterAsTwoUnits)
{
	const std::string emoji = "\xF0\x9F\x98\x80";
	const std::string head(126, 'a');

	CHECK(Internal::Truncated(head + emoji, 128) == head + emoji);
	CHECK(Internal::Truncated(head + emoji + "z", 128) == head + emoji);
	CHECK(Internal::Truncated(std::string(127, 'a') + emoji, 128) == std::string(127, 'a'));
	CHECK(Internal::Truncated(emoji + emoji, 3) == emoji);
	CHECK(Internal::Truncated(emoji, 1) == "");
}

TEST(Json_TruncatedPassesAnInvalidByteThrough)
{
	CHECK(Internal::Truncated("a\x80" "b", 128) == "a\x80" "b");
	CHECK(Internal::Truncated("a\x80" "b", 2) == "a\x80");
	CHECK(Internal::Truncated("ab\xE2\x82", 128) == "ab\xE2\x82");
	CHECK(Internal::Truncated("ab\xE2\x82", 3) == "ab\xE2");
	CHECK(Internal::Truncated("\xFF" "a", 1) == "\xFF");
}

TEST(Json_FrameNameIsTheEventOfAFrame)
{
	CHECK(Internal::FrameName("{\"cmd\":\"DISPATCH\",\"data\":{\"user\":{\"username\":\"x\"}},\"evt\":\"READY\",\"nonce\":null}")
		== "READY");
	CHECK(Internal::FrameName("{\"evt\" : \"ERROR\"}") == "ERROR");
}

TEST(Json_FrameNameFallsBackToTheCommandWhenTheEventIsNull)
{
	CHECK(Internal::FrameName("{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"3\"}") == "SET_ACTIVITY");
	CHECK(Internal::FrameName("{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"3\"}") == "SET_ACTIVITY");
}

TEST(Json_FrameNameRejectsAnythingButAFixedWord)
{
	CHECK(Internal::FrameName("").empty());
	CHECK(Internal::FrameName("{}").empty());
	CHECK(Internal::FrameName("{\"ping\":1}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"ready\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"Ready\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"READY2\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"A B\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"" + std::string(33, 'A') + "\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"" + std::string(32, 'A') + "\"}") == std::string(32, 'A'));
	CHECK(Internal::FrameName("{\"evt\":42,\"cmd\":\"SET_ACTIVITY\"}").empty());
	CHECK(Internal::FrameName("{\"evt\":\"READY").empty());
	CHECK(Internal::FrameName("{\"evt\":null,\"cmd\":\"x y\"}").empty());
}

TEST(Json_FrameNameNeverLetsTheBodyThrough)
{
	CHECK(Internal::FrameName("{\"data\":{\"user\":{\"username\":\"SECRET-USER\"}},\"evt\":\"READY\"}") == "READY");
	CHECK(Internal::FrameName("{\"data\":{\"username\":\"SECRET-USER\"}}").empty());
	CHECK(Internal::FrameName("{\"cmd\":\"SECRET-USER\"}").empty());
	CHECK(Internal::FrameName("{\"a\":\"evt\",\"evt\":\"READY\"}") == "READY");
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
