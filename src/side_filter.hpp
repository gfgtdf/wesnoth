/*
	Copyright (C) 2010 - 2025
	by Yurii Chernyi <terraninfo@terraninfo.net>
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

#include "units/conditional_type.hpp"
#include "variable.hpp"

#include <memory>
#include <string>
#include <vector>

class config;
class filter_context;
class unit_filter;
class team;

namespace side_filter_impl
{
	struct side_filter_args
	{
		const team& t;
		const filter_context* fc;
		bool use_flat_tod;

		const filter_context& context() const
		{
			if(fc) {
				return *fc;
			}
			throw std::runtime_error("filter_context is null");
		}

		side_filter_args(const team& t, const filter_context* fc, bool use_flat_tod)
			: t(t), fc(fc), use_flat_tod(use_flat_tod)
		{}
	};

	struct side_filter_base
	{
		virtual bool matches(const side_filter_args&) const = 0;
		virtual ~side_filter_base() {}
	};

	struct side_filter_compound : public side_filter_base
	{
		side_filter_compound(const vconfig& cfg);

		template<typename C, typename F>
		void create_attribute(const config::attribute_value& c, C conv, F func);
		template<typename F>
		void create_child(const vconfig& c, F func);

		void fill(const vconfig& cfg);

		virtual bool matches(const side_filter_args& args) const override;
		bool filter_impl(const side_filter_args& args) const;

		std::vector<std::shared_ptr<side_filter_base>> children_;
		std::vector<std::pair<conditional_type::type, side_filter_compound>> cond_children_;
	};
}

//side_filter: a class that implements the Standard Side Filter
class side_filter {
public:

	~side_filter();

	side_filter(const vconfig &cfg, const filter_context * fc, bool flat_tod = false);

	side_filter(const side_filter&) = default;
	side_filter& operator=(const side_filter&) = default;

	side_filter(side_filter&&) noexcept = default;
	side_filter& operator=(side_filter&&) noexcept = default;

	//match: returns true if and only if the given team matches this filter
	bool match(const team& t) const;
	bool match(const int side) const;
	std::vector<int> get_teams() const;
	const config& get_config() const {return cfg_.get_config();}

private:
	const vconfig cfg_; //config contains WML for a Standard Side Filter

	bool flat_;

	/** The filter context for this filter. It should be a pointer because otherwise the default ctor doesn't work */
	const filter_context * fc_;

	side_filter_impl::side_filter_compound impl_;
};
