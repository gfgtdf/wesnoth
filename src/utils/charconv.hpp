/*
	Copyright (C) 2024
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#pragma once

#include <boost/version.hpp>
#include <string_view>
#include <cctype>

// The gcc implemenetation of from_chars is in some versions just a temporaty solution that calls
// strtod that's why we prefer the boost version if available.
#if BOOST_VERSION >= 108500
	#define USE_BOOST_CHARCONV
#elif defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE >= 11
	#define USE_STD_CHARCONV
#elif defined(_MSC_VER) && _MSC_VER >= 1924
	#define USE_STD_CHARCONV
#elif defined(_LIBCPP_VERSION) && _LIBCPP_VERSION >= 14000
	#define USE_FALLBACK_CHARCONV
#else
	#error No charconv implementation found.
#endif

#ifdef USE_BOOST_CHARCONV
#include <boost/charconv.hpp>
namespace charconv_impl = boost::charconv;
#else
#include <charconv>
namespace charconv_impl = std;
#endif


namespace utils::charconv
{
	using chars_format      = charconv_impl::chars_format;
	using from_chars_result = charconv_impl::from_chars_result;
	using to_chars_result   = charconv_impl::to_chars_result;

	template<typename... T>
	to_chars_result to_chars(char* first, char* last, T&&... value )
	{
		return charconv_impl::to_chars(first, last, value...);
	}

	template<typename T>
	std::enable_if_t<std::is_integral_v<T>, from_chars_result> from_chars(const char* first, const char* last, T& value, int base = 10 )
	{
		return charconv_impl::from_chars(first, last, value, base);
	}

#ifndef USE_FALLBACK_CHARCONV
	template<typename T>
	std::enable_if_t<std::is_floating_point_v<T>, from_chars_result> from_chars(const char* first, const char* last, T& value, chars_format fmt = chars_format::general )
	{
		return charconv_impl::from_chars(first, last, value, fmt);
	}
#else
	template<typename T>
	std::enable_if_t<std::is_same_v<T, double> || std::is_same_v<T, float> || std::is_same_v<T, long double>, from_chars_result> from_chars(const char* first, const char* last, T& value, chars_format fmt = chars_format::general );
#endif

	// the maximum size of a string that to_chars produces for type T, with the default chars_format
	template<class T>
	constexpr size_t buffer_size = 50;
}

namespace utils {
	inline void trim_for_from_chars(std::string_view& v)
	{
		while(!v.empty() && std::isspace(v.front())) {
			v.remove_prefix(1);
		}
		if(v.size() >= 2 && v[0] == '+' && v[1] != '-' ) {
			v.remove_prefix(1);
		}
	}
}
