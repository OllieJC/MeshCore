#include <gtest/gtest.h>
#include <helpers/sensors/LPPDataHelpers.h>

TEST(ParticulateMatter, WritesTheThreeAtmosphericBinsInNetworkOrder) {
  uint8_t data[8] = {};
  LPPWriter writer(data, sizeof(data));
  ASSERT_TRUE(writer.writeParticulateMatter(7, 0, 0x0123, 65535));
  const uint8_t expected[] = {7, 228, 0, 0, 1, 0x23, 0xff, 0xff};
  EXPECT_EQ(writer.length(), sizeof(expected));
  EXPECT_EQ(memcmp(data, expected, sizeof(expected)), 0);
  LPPReader reader(data, writer.length());
  uint8_t channel, type;
  ASSERT_TRUE(reader.readHeader(channel, type));
  EXPECT_EQ(channel, 7);
  EXPECT_EQ(type, LPP_AIR_QUALITY);
  EXPECT_EQ(LPPData::getDataSize(type), 6);
  uint16_t pm1, pm25, pm10;
  ASSERT_TRUE(reader.readParticulateMatter(pm1, pm25, pm10));
  EXPECT_EQ(pm1, 0);
  EXPECT_EQ(pm25, 0x0123);
  EXPECT_EQ(pm10, 65535);
}

TEST(ParticulateMatter, RejectsShortBuffersAndScalarWrites) {
  uint8_t data[7] = {7, LPP_AIR_QUALITY};
  LPPWriter writer(data, sizeof(data));
  EXPECT_FALSE(writer.writeParticulateMatter(7, 1, 2, 3));
  EXPECT_FALSE(writer.writeData(7, LPP_AIR_QUALITY, 1));
  EXPECT_EQ(writer.length(), 0);
  LPPReader reader(data, sizeof(data));
  uint8_t channel, type;
  ASSERT_TRUE(reader.readHeader(channel, type));
  uint16_t pm1 = 9, pm25 = 9, pm10 = 9;
  EXPECT_FALSE(reader.readParticulateMatter(pm1, pm25, pm10));
  EXPECT_EQ(pm1, 9);
  EXPECT_EQ(pm25, 9);
  EXPECT_EQ(pm10, 9);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
