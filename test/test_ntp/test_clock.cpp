#include <gtest/gtest.h>
#include <helpers/ntp/NtpClock.h>

class TestClock : public mesh::RTCClock {
public:
  uint32_t now = 1000;
  int adjustments = 0;

  uint32_t getCurrentTime() override { return now; }
  void setCurrentTime(uint32_t time) override {
    now = time;
    ++adjustments;
  }
};

TEST(NtpClock, RecordsRepliesAndOnlyAdvancesTheClock) {
  TestClock clock;
  ntp::LastReply reply;
  EXPECT_EQ(reply.received, 0U);

  reply.accept(clock, 900);
  EXPECT_EQ(reply.received, 900U);
  EXPECT_TRUE(reply.clockAhead);
  EXPECT_EQ(clock.now, 1000U);
  EXPECT_EQ(clock.adjustments, 0);

  reply.accept(clock, 1000);
  EXPECT_FALSE(reply.clockAhead);
  EXPECT_EQ(clock.adjustments, 0);

  reply.accept(clock, 1100);
  EXPECT_EQ(reply.received, 1100U);
  EXPECT_FALSE(reply.clockAhead);
  EXPECT_EQ(clock.now, 1100U);
  EXPECT_EQ(clock.adjustments, 1);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
