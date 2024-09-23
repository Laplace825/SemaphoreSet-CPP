#pragma once

#include <format>
#include <iostream>
#include <string_view>

namespace lap {

template < typename... ArgsT >
void print(const std::string_view fmt, ArgsT &&...args) {
    std::cout << std::vformat(fmt, std::make_format_args(args...));
}

template < typename... ArgsT >
void println(const std::string_view fmt, ArgsT &&...args) {
    std::cout << std::vformat(fmt, std::make_format_args(args...)) << std::endl;
}

} // namespace lap