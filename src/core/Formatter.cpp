#include "instrument-script-server/core/Formatter.hpp"
#include <cmath>
#include <iomanip>

Formatter::Formatter(instserver::Format fmt) : m_fmt(std::move(fmt)) {}

std::string Formatter::replaceExponent(std::string str) const {
  auto pos = str.find_first_of("eE");

  if (pos != std::string::npos) {
    str[pos] = m_fmt.exponent_char;
  }

  return str;
}

template <typename T> std::string Formatter::formatEngineering(T value) const {
  double mantissa = 0.0;
  int exponent = 0;

  if (value != static_cast<T>(0)) {
    exponent = static_cast<int>(std::floor(
                   std::log10(std::fabs(static_cast<double>(value))) / 3.0)) *
               3;

    mantissa = static_cast<double>(value) / std::pow(10.0, exponent);
  }

  std::ostringstream oss;

  if (m_fmt.significant_digits.has_value()) {
    oss << std::setprecision(m_fmt.significant_digits.value());
  }

  oss << mantissa;

  char exp_buf[32];

  std::snprintf(exp_buf, sizeof(exp_buf), "%c%+03d", m_fmt.exponent_char,
                exponent);

  return oss.str() + exp_buf;
}

std::string Formatter::format(bool value) const {
  if (value) {
    return m_fmt.high_representation.value_or("true");
  }

  return m_fmt.low_representation.value_or("false");
}

std::string Formatter::format(int32_t value) const {
  return format(static_cast<int64_t>(value));
}

std::string Formatter::format(int64_t value) const {
  std::ostringstream oss;

  switch (m_fmt.notation) {
  case instserver::Notation::Scientific:
    return format(static_cast<double>(value));

  case instserver::Notation::Engineering:
    return formatEngineering(value);

  case instserver::Notation::Auto:
    if (m_fmt.significant_digits.has_value()) {
      oss << std::setprecision(m_fmt.significant_digits.value());
    }
    break;

  default:
    break;
  }

  oss << value;

  return replaceExponent(oss.str());
}

std::string Formatter::format(float value) const {
  return format(static_cast<double>(value));
}

std::string Formatter::format(double value) const {
  if (m_fmt.notation == instserver::Notation::Engineering) {
    return formatEngineering(value);
  }

  std::ostringstream oss;

  switch (m_fmt.notation) {
  case instserver::Notation::Fixed:
    oss << std::fixed;

    if (m_fmt.decimal_places.has_value()) {
      oss << std::setprecision(m_fmt.decimal_places.value());
    }
    break;

  case instserver::Notation::Scientific:
    oss << std::scientific;

    if (m_fmt.significant_digits.has_value()) {
      oss << std::setprecision(m_fmt.significant_digits.value() - 1);
    }
    break;

  case instserver::Notation::Auto:
    if (m_fmt.significant_digits.has_value()) {
      oss << std::setprecision(m_fmt.significant_digits.value());
    }
    break;

  default:
    break;
  }

  oss << value;

  return replaceExponent(oss.str());
}

std::string Formatter::format(const char *value) const {
  return value ? std::string(value) : std::string();
}

std::string Formatter::format(const std::string &value) const { return value; }

/*
 * Explicit template instantiations
 */
template std::string Formatter::formatEngineering<int32_t>(int32_t) const;
template std::string Formatter::formatEngineering<int64_t>(int64_t) const;
template std::string Formatter::formatEngineering<float>(float) const;
template std::string Formatter::formatEngineering<double>(double) const;
