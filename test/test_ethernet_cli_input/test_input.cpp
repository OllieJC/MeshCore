#include <gtest/gtest.h>
#include <helpers/ethernet/EthernetCLIInput.h>

class EthernetInputTest : public ::testing::Test {
protected:
  EthernetCLIInput input;
  char buffer[12] = {};

  EthernetCLIInput::Result feed(const char* text) {
    auto result = EthernetCLIInput::Pending;
    while (*text) result = input.consume(*text++, buffer, sizeof(buffer), "test-key");
    return result;
  }
};

TEST_F(EthernetInputTest, RequiresLoginAndClearsPasswordBeforeDispatch) {
  EXPECT_EQ(feed("filter\r"), EthernetCLIInput::Denied);
  EXPECT_EQ(feed("test-"), EthernetCLIInput::Pending);
  EXPECT_EQ(feed("key\r"), EthernetCLIInput::LoggedIn);
  EXPECT_STREQ(buffer, "");
  EXPECT_EQ(feed("\n"), EthernetCLIInput::Pending);
  EXPECT_EQ(feed("filter\n"), EthernetCLIInput::Command);
  EXPECT_STREQ(buffer, "filter");
}

TEST_F(EthernetInputTest, RejectsTheWholeInvalidLineAndResumesAtTheNext) {
  ASSERT_EQ(feed("test-key\n"), EthernetCLIInput::LoggedIn);
  EXPECT_EQ(feed("12345678901\r"), EthernetCLIInput::Command);
  EXPECT_STREQ(buffer, "12345678901");
  EXPECT_EQ(feed("123456789012\r"), EthernetCLIInput::InvalidLine);
  EXPECT_EQ(feed("erase"), EthernetCLIInput::Pending);
  EXPECT_EQ(input.consume(0, buffer, sizeof(buffer), "test-key"), EthernetCLIInput::Pending);
  EXPECT_EQ(feed("ignored\n"), EthernetCLIInput::InvalidLine);
  EXPECT_EQ(feed("filter\n"), EthernetCLIInput::Command);
  EXPECT_STREQ(buffer, "filter");
}

TEST_F(EthernetInputTest, NewConnectionDiscardsPartialCommandsAndNeedsItsOwnLogin) {
  ASSERT_EQ(feed("test-key\n"), EthernetCLIInput::LoggedIn);
  EXPECT_EQ(feed("pass"), EthernetCLIInput::Pending);
  input = EthernetCLIInput();
  EXPECT_EQ(feed("word\n"), EthernetCLIInput::Denied);
  EXPECT_EQ(feed("test-key\n"), EthernetCLIInput::LoggedIn);
  EXPECT_EQ(feed("filter list\n"), EthernetCLIInput::Command);
  EXPECT_STREQ(buffer, "filter list");
}

TEST_F(EthernetInputTest, RejectsFactoryOrEmptyPasswordEvenForAnExistingSession) {
  ASSERT_EQ(feed("test-key\n"), EthernetCLIInput::LoggedIn);
  EXPECT_EQ(input.consume('f', buffer, sizeof(buffer), "password"), EthernetCLIInput::Denied);
  EXPECT_EQ(input.consume('f', buffer, sizeof(buffer), ""), EthernetCLIInput::Denied);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
