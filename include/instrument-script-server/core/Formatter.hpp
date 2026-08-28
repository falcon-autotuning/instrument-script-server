#pragma once

#include "instrument-script-server/core/ParsingTools.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

class Formatter {
public:
  explicit Formatter(instserver::Format fmt);

  [[nodiscard]] std::string format(bool value) const;
  [[nodiscard]] std::string format(int32_t value) const;
  [[nodiscard]] std::string format(int64_t value) const;
  [[nodiscard]] std::string format(float value) const;
  [[nodiscard]] std::string format(double value) const;

  std::string format(const char *value) const;
  [[nodiscard]] std::string format(const std::string &value) const;

  template <size_t N> std::string format(const char (&value)[N]) const {
    return std::string(value);
  }

private:
  instserver::Format m_fmt;

  [[nodiscard]] std::string replaceExponent(std::string str) const;

  template <typename T> std::string formatEngineering(T value) const;
};
