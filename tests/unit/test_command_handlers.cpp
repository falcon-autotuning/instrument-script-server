#include "instrument-script-server/daemon/CommandHandlers.hpp"

#include <gtest/gtest.h>

using instserver::daemon::v1::VariableValue;

namespace {

VariableValue make_string(const std::string &value) {
  VariableValue var;
  var.set_s(value);
  return var;
}

VariableValue make_double(double value) {
  VariableValue var;
  var.set_d(value);
  return var;
}

} // namespace

TEST(CommandHandlersTest, ConvertsMixedArrayItemsRecursively) {
  sol::state lua;
  lua.open_libraries(sol::lib::base);

  VariableValue item;
  (*item.mutable_m_map()->mutable_values())["id"] = make_string("Source1");
  (*item.mutable_m_map()->mutable_values())["channel"] = make_double(1.0);

  VariableValue array;
  array.mutable_m_array()->add_values()->CopyFrom(item);

  sol::object converted = instserver::daemon::variable_to_lua(lua, &array);
  ASSERT_EQ(converted.get_type(), sol::type::table);

  sol::table table = converted.as<sol::table>();
  sol::object first = table[1];
  ASSERT_EQ(first.get_type(), sol::type::table);

  sol::table first_table = first.as<sol::table>();
  EXPECT_EQ(first_table["id"].get<std::string>(), "Source1");
  EXPECT_DOUBLE_EQ(first_table["channel"].get<double>(), 1.0);
}
