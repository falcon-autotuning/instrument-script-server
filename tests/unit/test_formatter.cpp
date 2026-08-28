#include "instrument-script-server/core/Formatter.hpp"

#include <gtest/gtest.h>

using namespace instserver;
TEST(Formatter, BoolDefaultTrue) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(true), "true");
}

TEST(Formatter, BoolDefaultFalse) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(false), "false");
}

TEST(Formatter, BoolCustomRepresentation) {
  Format f;
  f.high_representation = "ON";
  f.low_representation = "OFF";

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(true), "ON");
  EXPECT_EQ(fmt.format(false), "OFF");
}
TEST(Formatter, CStringFormatting) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format("hello"), "hello");
}

TEST(Formatter, NullCStringFormatting) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(static_cast<const char *>(nullptr)), "");
}

TEST(Formatter, StdStringFormatting) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(std::string("hello")), "hello");
}

TEST(Formatter, CharArrayFormatting) {
  Formatter fmt(Format{});

  const char text[] = "abc";

  EXPECT_EQ(fmt.format(text), "abc");
}
TEST(Formatter, Int64Auto) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(int64_t(12345)), "12345");
}

TEST(Formatter, Int32DelegatesToInt64) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(int32_t(42)), "42");
}
TEST(Formatter, IntScientific) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(int64_t(1234)), "1.234E+03");
}
TEST(Formatter, ScientificCustomExponentCharacter) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 4;
  f.exponent_char = 'e';

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.0), "1.234e+03");
}
TEST(Formatter, FixedNotationTwoDecimals) {
  Format f;
  f.notation = Notation::Fixed;
  f.decimal_places = 2;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.0), "1234.00");
}
TEST(Formatter, ScientificDouble) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 5;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.567), "1.2346E+03");
}
TEST(Formatter, ScientificNegativeDouble) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(-1234.0), "-1.234E+03");
}
TEST(Formatter, AutoSignificantDigits) {
  Format f;
  f.notation = Notation::Auto;
  f.significant_digits = 4;

  Formatter fmt(f);

  const std::string result = fmt.format(1234.567);

  EXPECT_FALSE(result.empty());
}
TEST(Formatter, EngineeringDouble) {
  Format f;
  f.notation = Notation::Engineering;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.0), "1.234E+03");
}

TEST(Formatter, EngineeringSmallValue) {
  Format f;
  f.notation = Notation::Engineering;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(0.001234), "1.234E-03");
}
TEST(Formatter, EngineeringZero) {
  Format f;
  f.notation = Notation::Engineering;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(0.0), "0E+00");
}
TEST(Formatter, EngineeringUsesCustomExponent) {
  Format f;
  f.notation = Notation::Engineering;
  f.exponent_char = 'e';

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1000.0), "1e+03");
}
TEST(Formatter, FloatFormatting) {
  Format f;
  f.notation = Notation::Fixed;
  f.decimal_places = 2;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.567), "1234.57");
}
TEST(Formatter, ZeroScientific) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(0.0), "0.000E+00");
}
TEST(Formatter, LargeValueScientific) {
  Format f;
  f.notation = Notation::Scientific;
  f.significant_digits = 6;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1.23e12), "1.23000E+12");
}
TEST(Formatter, BoolOnlyHighRepresentation) {
  Format f;
  f.high_representation = "ON";

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(true), "ON");
  EXPECT_EQ(fmt.format(false), "false");
}
TEST(Formatter, BoolOnlyLowRepresentation) {
  Format f;
  f.low_representation = "OFF";

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(true), "true");
  EXPECT_EQ(fmt.format(false), "OFF");
}
TEST(Formatter, ExponentReplacementNotNeeded) {
  Formatter fmt(Format{});

  EXPECT_EQ(fmt.format(1234), "1234");
}
TEST(Formatter, ScientificWithoutPrecision) {
  Format f;
  f.notation = Notation::Scientific;

  Formatter fmt(f);

  EXPECT_FALSE(fmt.format(1234.0).empty());
}
TEST(Formatter, FixedWithoutDecimalsSpecified) {
  Format f;
  f.notation = Notation::Fixed;

  Formatter fmt(f);

  EXPECT_FALSE(fmt.format(12.34).empty());
}
TEST(Formatter, AutoWithoutPrecision) {
  Format f;
  f.notation = Notation::Auto;

  Formatter fmt(f);

  EXPECT_FALSE(fmt.format(123.456).empty());
}
TEST(Formatter, EngineeringNegative) {
  Format f;
  f.notation = Notation::Engineering;
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(-1234.0), "-1.234E+03");
}
TEST(Formatter, EngineeringInt64) {
  Format f;
  f.notation = Notation::Engineering;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(int64_t(1000)), "1E+03");
}
TEST(Formatter, CustomExponentCharacterUppercase) {
  Format f;
  f.notation = Notation::Scientific;
  f.exponent_char = '#';
  f.significant_digits = 4;

  Formatter fmt(f);

  EXPECT_EQ(fmt.format(1234.0), "1.234#+03");
}
TEST(Formatter, EngineeringActuallyComputesMantissaAndExponent) {
  Format f;
  f.notation = Notation::Engineering;

  Formatter fmt(f);

  EXPECT_NE(fmt.format(1234.0), "0E+00");
}
