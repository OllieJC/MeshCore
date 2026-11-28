#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>
#include "../../examples/m7ojc_sensor/SensorWatch.h"
#include "../../examples/simple_repeater/KeyedChannel.h"
#include <helpers/TxtDataHelpers.h>

namespace {
class Settings : public KeyValueStore {
public:
  std::map<std::string, std::string> values;
  bool writable = true;
  bool getByKey(const char* key, char* value, size_t size) override {
    auto entry = values.find(key);
    if (entry == values.end() || entry->second.size() >= size) return false;
    strcpy(value, entry->second.c_str());
    return true;
  }
  bool setByKey(const char* key, const char* value) override {
    if (writable) values[key] = value;
    return writable;
  }
};

mesh::GroupChannel channel(const char* name) {
  mesh::GroupChannel ch{};
  deriveChannelKey(name, ch.secret, ch.hash);
  return ch;
}

class WatchTest : public ::testing::Test {
protected:
  Settings store;
  SensorWatch watch;
  void SetUp() override { watch.begin(store); }

  std::string cli(const std::string& command) {
    char reply[CLI_REPLY_MAX + 1]{};
    reply[CLI_REPLY_MAX] = '!';
    EXPECT_TRUE(watch.handleCommand(command.c_str(), reply));
    EXPECT_EQ(reply[CLI_REPLY_MAX], '!');
    return reply;
  }

  bool receive(const char* name, const std::string& body,
               uint8_t type = PAYLOAD_TYPE_GRP_TXT, uint8_t flags = TXT_TYPE_PLAIN) {
    std::vector<uint8_t> packet(5);
    packet[4] = flags << 2;
    packet.insert(packet.end(), body.begin(), body.end());
    return watch.matches(type, channel(name), packet.data(), packet.size());
  }
};

TEST_F(WatchTest, UpgradeKeepsTheThreeOriginalRequests) {
  EXPECT_EQ(cli("sEnSoR.WaTcH"), "sensor.watch on, 1 rule");
  EXPECT_EQ(cli("SENSOR.WATCH LiSt"), "1 #m7ojc wx,sensor,sensors");
  for (auto word : {"Wx", "sEnSoR", "SENSORS"})
    EXPECT_TRUE(receive("#m7ojc", std::string("Alice: ") + word));
  EXPECT_FALSE(receive("#m7ojc", "wx: hello"));
  EXPECT_FALSE(receive("#weather", "Alice: WX"));
  EXPECT_FALSE(receive("#m7ojc", "Alice: sensors please"));
  char reply[CLI_REPLY_MAX];
  EXPECT_FALSE(watch.handleCommand("sensor.watcher on", reply));
  EXPECT_FALSE(watch.handleCommand("pm", reply));
}

TEST_F(WatchTest, ExamplesAreWholeMessageCaseInsensitiveAlternativesOnTheirOwnChannels) {
  EXPECT_EQ(cli("SENSOR.WATCH ADD #M7OJC WX,SENSOR"), "OK - added rule 2");
  EXPECT_EQ(cli("Sensor.Watch Add #Weather WX da7*, wx Bexley* ,Wx M7OJC"), "OK - added rule 3");
  EXPECT_EQ(cli("sensor.watch add #WEATHER wx da7*,wx bexley*,wx m7ojc"), "OK - rule 3 already exists");
  EXPECT_EQ(cli("SENSOR.watch"), "sensor.watch on, 3 rules");
  EXPECT_EQ(cli("Sensor.WaTch LIST 3"), "3 #weather wx da7*,wx bexley*,wx m7ojc");
  for (auto body : {"WX DA7", "Wx dA7 6AB", " wx BeXlEyHeAtH  ", "wX M7oJc"})
    EXPECT_TRUE(receive("#weather", std::string("Alice: ") + body)) << body;
  for (auto body : {"wx", "please wx da7", "wx m7ojc extra", "sensors"})
    EXPECT_FALSE(receive("#weather", std::string("Alice: ") + body)) << body;
  EXPECT_FALSE(receive("#m7ojc", "Alice: wx da7"));
  EXPECT_FALSE(receive("#unknown", "Alice: wx da7"));
}

TEST_F(WatchTest, WildcardsAreBoundedToPlainSingleLineRequests) {
  EXPECT_EQ(cli("sensor.watch add #weather *DA*7,w*x?"), "OK - added rule 2");
  EXPECT_TRUE(receive("#weather", "Alice: A dA Da 7"));
  EXPECT_TRUE(receive("#weather", "Alice: wx?"));
  EXPECT_FALSE(receive("#weather", "Alice: wx!"));
  EXPECT_EQ(cli("sensor.watch add #weather *"), "OK - added rule 3");
  EXPECT_TRUE(receive("#weather", "Alice: anything"));
  EXPECT_FALSE(receive("#weather", "Alice: "));
  EXPECT_FALSE(receive("#weather", "missing sender"));
  EXPECT_FALSE(receive("#weather", "Node: 20 °C\nCO2 500 ppm"));
  EXPECT_FALSE(receive("#weather", "Alice: \r\nWX"));
  EXPECT_FALSE(receive("#weather", "Alice: wx", PAYLOAD_TYPE_GRP_DATA));
  EXPECT_FALSE(receive("#weather", "Alice: wx", PAYLOAD_TYPE_GRP_TXT, TXT_TYPE_CLI_COMMAND));
  const char padded[] = "Alice: Wx\0ignored bytes after padding";
  EXPECT_TRUE(receive("#m7ojc", std::string(padded, sizeof(padded))));
}

TEST_F(WatchTest, OffAndDeletionSurviveReloadAndDoNotRenumberOtherRules) {
  EXPECT_EQ(cli("sENSOR.WATCH aDd #weather wx"), "OK - added rule 2");
  EXPECT_EQ(cli("sENSOR.WATCH oFf"), "OK - sensor.watch off");
  watch.begin(store);
  EXPECT_EQ(cli("sensor.watch"), "sensor.watch off, 2 rules");
  const auto weather = channel("#weather");
  mesh::GroupChannel out[4];
  EXPECT_EQ(watch.searchChannels(weather.hash, out, 4), 0);
  EXPECT_FALSE(receive("#weather", "Alice: wx"));
  EXPECT_EQ(cli("sensor.watch dEl 1"), "OK - deleted rule 1");
  EXPECT_EQ(cli("sensor.watch On"), "OK - sensor.watch on");
  watch.begin(store);
  EXPECT_EQ(cli("sensor.watch LIST"), "2 #weather wx");
  EXPECT_FALSE(receive("#m7ojc", "Alice: wx"));
  EXPECT_TRUE(receive("#weather", "Alice: WX"));
  EXPECT_EQ(cli("sensor.watch deL 2"), "OK - deleted rule 2");
  watch.begin(store);
  EXPECT_EQ(cli("sensor.watch"), "sensor.watch on, 0 rules");
  EXPECT_EQ(cli("sensor.watch list"), "No watch rules");
  EXPECT_EQ(cli("sensor.watch add #other wx"), "OK - added rule 1");
}

TEST_F(WatchTest, InvalidOrUncommittedChangesLeaveTheWorkingRulesIntact) {
  for (auto args : {"add", "add #weather", "add weather wx", "add # wx", "add #weather wx,,pm",
                    "add #weather wx, ", "add #weather ,wx", "del", "del 0", "del 2x",
                    "del 1 extra", "list 9", "on extra", "off extra", "add #weather \"wx"}) {
    EXPECT_EQ(cli(std::string("SENSOR.WATCH ") + args).find("Err"), 0U) << args;
  }
  EXPECT_EQ(cli("sensor.watch add #weather " + std::string(120, 'a')).find("Err"), 0U);
  EXPECT_EQ(cli("sensor.watch add #weather " + std::string(200, 'a')).find("Err"), 0U);
  store.writable = false;
  EXPECT_EQ(cli("sensor.watch del 1").find("Err"), 0U);
  EXPECT_EQ(cli("sensor.watch OFF").find("Err"), 0U);
  EXPECT_EQ(cli("sensor.watch add #weather wx").find("Err"), 0U);
  EXPECT_EQ(cli("sensor.watch list"), "1 #m7ojc wx,sensor,sensors");
  watch.begin(store);
  EXPECT_TRUE(receive("#m7ojc", "Alice: wx"));
}

TEST_F(WatchTest, ListPaginatesWithoutTruncatingRulesAndReusesOnlyDeletedIDs) {
  const std::string long_spec = "#m7ojc " + std::string(120, 'a');
  EXPECT_EQ(cli("sensor.watch add " + long_spec), "OK - added rule 2");
  for (int i = 3; i <= 8; ++i) {
    EXPECT_EQ(cli("sensor.watch add #m7ojc wx" + std::to_string(i)), "OK - added rule " + std::to_string(i));
  }
  EXPECT_EQ(cli("sensor.watch add #m7ojc wx9").find("Err"), 0U);
  EXPECT_EQ(cli("sensor.watch list"), "1 #m7ojc wx,sensor,sensors\nnext=2");
  const auto second = cli("sensor.watch list 2");
  EXPECT_EQ(second.find("2 " + long_spec), 0U);
  ASSERT_NE(second.find("next="), std::string::npos);
  const auto next = second.substr(second.find("next=") + 5);
  EXPECT_NE(cli("sensor.watch list " + next).find("8 #m7ojc wx8"), std::string::npos);
  EXPECT_EQ(cli("sensor.watch del 3"), "OK - deleted rule 3");
  EXPECT_EQ(cli("sensor.watch add #local temp"), "OK - added rule 3");
  EXPECT_EQ(cli("sensor.watch list 8"), "8 #m7ojc wx8");
}

TEST_F(WatchTest, HashCandidatesAreUniqueAndTheFullChannelKeyControlsTheMatch) {
  const auto first = channel("#m7ojc");
  ASSERT_EQ(cli("sensor.watch add #m7ojc wx*"), "OK - added rule 2");
  mesh::GroupChannel out[4];
  EXPECT_EQ(watch.searchChannels(first.hash, out, 0), 0);
  ASSERT_EQ(watch.searchChannels(first.hash, out, 4), 1);
  EXPECT_EQ(memcmp(out[0].secret, first.secret, sizeof(first.secret)), 0);
  int collisions = 0;
  for (int i = 0; i < 10000 && collisions < 4; ++i) {
    const auto name = "#ch" + std::to_string(i);
    const auto candidate = channel(name.c_str());
    if (first.hash[0] != candidate.hash[0] || !memcmp(candidate.secret, first.secret, sizeof(first.secret)))
      continue;
    EXPECT_FALSE(receive(name.c_str(), "Alice: WX"));
    const auto result = cli("sensor.watch add " + name + " pm");
    if (++collisions == 4) {
      EXPECT_EQ(result.find("Err"), 0U);
    } else {
      ASSERT_EQ(result.find("OK"), 0U);
      EXPECT_TRUE(receive(name.c_str(), "Alice: pm"));
      EXPECT_FALSE(receive(name.c_str(), "Alice: wx"));
    }
  }
  ASSERT_EQ(collisions, 4);
  EXPECT_EQ(watch.searchChannels(first.hash, out, 4), 4);
  EXPECT_EQ(watch.searchChannels(first.hash, out, 1), 1);
  EXPECT_EQ(cli("sensor.watch add #m7ojc temp"), "OK - added rule 6");
}
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
