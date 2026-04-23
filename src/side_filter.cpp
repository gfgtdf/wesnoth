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

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "config.hpp"
#include "display_context.hpp"
#include "filter_context.hpp"
#include "log.hpp"
#include "recall_list_manager.hpp"
#include "side_filter.hpp"
#include "variable.hpp"
#include "team.hpp"
#include "serialization/string_utils.hpp"
#include "scripting/game_lua_kernel.hpp"
#include "play_controller.hpp"
#include "resources.hpp"
#include "synced_context.hpp"
#include "units/unit.hpp"
#include "units/filter.hpp"
#include "units/map.hpp"
#include "formula/callable_objects.hpp"
#include "formula/formula.hpp"
#include "formula/function_gamestate.hpp"
#include "utils/general.hpp"

static lg::log_domain log_engine_sf("engine/side_filter");
#define ERR_NG LOG_STREAM(err, log_engine_sf)

static lg::log_domain log_wml("wml");
#define ERR_WML LOG_STREAM(err, log_wml)

using namespace side_filter_impl;

side_filter::~side_filter() {}

side_filter::side_filter(const vconfig& cfg, const filter_context * fc,  bool flat_tod)
	: cfg_(cfg)
	, flat_(flat_tod)
	, fc_(fc)
	, impl_(cfg_)
{
}

std::vector<int> side_filter::get_teams() const
{
	assert(fc_);
	//@todo: replace with better implementation
	std::vector<int> result;
	for(const team &t : fc_->get_disp_context().teams()) {
		if (match(t)) {
			result.push_back(t.side());
		}
	}
	return result;
}

bool side_filter::match(int side) const
{
	assert(fc_);
	return this->match((fc_->get_disp_context().get_team(side)));
}

bool side_filter::match(const team& t) const
{
	return impl_.matches(side_filter_impl::side_filter_args{t, fc_, flat_});
}

namespace {

static bool check_side_number(const team &t, const std::string &str)
{
	return in_ranges(t.side(), utils::parse_ranges_unsigned(str));
}

template<typename F>
struct side_filter_child_literal : public side_filter_base
{
	side_filter_child_literal(const vconfig& v, const F& f) : v_(v) , f_(f) {}
	virtual bool matches(const side_filter_args& args) const override
	{
		return f_(v_, args);
	}
	vconfig v_;
	F f_;
};

template<typename T, typename F>
struct side_filter_attribute_parsed : public side_filter_base
{
	side_filter_attribute_parsed(T&& v, F&& f) : v_(std::move(v)), f_(std::move(f)) {}
	virtual bool matches(const side_filter_args& args) const override
	{
		return f_(v_, args);
	}
	T v_;
	F f_;
};

template<typename C, typename F>
struct side_filter_attribute_literal : public side_filter_base
{
	side_filter_attribute_literal(std::string&& v, C&& c, F&& f) : v_(std::move(v)), c_(std::move(c)), f_(std::move(f)) {}
	virtual bool matches(const side_filter_args& args) const override
	{
		config::attribute_value v;
		v = utils::interpolate_variables_into_string(v_, *(resources::gamedata));
		return f_(c_(v), args);
	}
	std::string v_;
	C c_;
	F f_;
};

class contains_dollar_visitor
#ifdef USING_BOOST_VARIANT
	: public boost::static_visitor<bool>
#endif
{
public:
	contains_dollar_visitor() {}

	template<typename T>
	bool operator()(const T&) const { return false; }

	bool operator()(const t_string&)    const { return true; }

	bool operator()(const std::string& s) const
	{
		return s.find('$') != std::string::npos;
	}
};

}

side_filter_compound::side_filter_compound(const vconfig& cfg)
	: children_()
	, cond_children_()
{
	fill(cfg);
}

bool side_filter_compound::matches(const side_filter_args& args) const
{
	bool res = filter_impl(args);

	// Handle [and], [or], and [not] with in-order precedence
	for(const auto & filter : cond_children_) {
		switch (filter.first) {
		case conditional_type::type::filter_and:
			res = res && filter.second.matches(args);
			break;
		case conditional_type::type::filter_or:
			res = res || filter.second.matches(args);
			break;
		case conditional_type::type::filter_not:
			res = res && !filter.second.matches(args);
			break;
		}
	}
	return res;
}

bool side_filter_compound::filter_impl(const side_filter_args& args) const
{
	for(const auto & filter : children_) {
		if (!filter->matches(args)) {
			return false;
		}
	}
	return true;
}

template<typename F>
void side_filter_compound::create_child(const vconfig& c, F func)
{
	children_.emplace_back(new side_filter_child_literal<F>(c, func));
}

template<typename C, typename F>
void side_filter_compound::create_attribute(const config::attribute_value& v, C conv, F func)
{
	if(v.blank()) {
	}
	else if(v.apply_visitor(contains_dollar_visitor())) {
		children_.emplace_back(new side_filter_attribute_literal<C, F>(std::move(v.str()), std::move(conv), std::move(func)));
	}
	else {
		children_.emplace_back(new side_filter_attribute_parsed<decltype(conv(v)), F>(std::move(conv(v)), std::move(func)));
	}
}

void side_filter_compound::fill(const vconfig& cfg)
{
	const config& literal = cfg.get_config();

	//optimisation
	if (literal.empty()) { return; }

	create_attribute(literal["side_in"],
		[](const config::attribute_value& c) { return c.str(); },
		[](const std::string& str, const side_filter_args& args)
		{
			return check_side_number(args.t, str);
		}
	);

	create_attribute(literal["side"],
		[](const config::attribute_value& c) { return c.str(); },
		[](const std::string& str, const side_filter_args& args)
		{
			return check_side_number(args.t, str);
		}
	);

	create_attribute(literal["team_name"],
		[](const config::attribute_value& c) { return c.str(); },
		[](const std::string& that_team_name, const side_filter_args& args)
		{
			const std::string& this_team_name = args.t.team_name();

			if(!utils::contains(this_team_name, ',')) {
				return this_team_name == that_team_name;
			}
			else {
				const std::vector<std::string>& these_team_names = utils::split(this_team_name);
				for(const std::string& this_single_team_name : these_team_names) {
					if(this_single_team_name == that_team_name) {
						return true;
					}
				}
				return false;
			}
		}
	);

	create_attribute(literal["controller"],
		[](const config::attribute_value& c) { return utils::split(c.str()); },
		[](const std::vector<std::string>& controllers, const side_filter_args& args)
		{
			if (resources::controller->is_networked_mp() && synced_context::is_synced()) {
				ERR_NG << "ignoring controller= in SSF due to danger of OOS errors";
				return true;
			}
			else {
				for(const std::string& controller : controllers)
				{
					if(side_controller::get_string(args.t.controller()) == controller) {
						return true;
					}
				}
				return false;
			}
		}
	);

	create_attribute(literal["formula"],
		[](const config::attribute_value& c)
		{
			try {
				return wfl::formula(c, new wfl::gamestate_function_symbol_table(), true);
			} catch(const wfl::formula_error& e) {
				lg::log_to_chat() << "Formula error while evaluating formula in side filter: " << e.type << " at "
								  << e.filename << ':' << e.line << ")\n";
				ERR_WML << "Formula error while evaluating formula in side filter: " << e.type << " at "
						<< e.filename << ':' << e.line << ")";
				return wfl::formula("");
			}
		},
		[](const wfl::formula& form, const side_filter_args& args)
		{
			try {
				const wfl::team_callable callable(args.t);
				if(!form.evaluate(callable).as_bool()) {
					return false;
				}
				return true;
			} catch(const wfl::formula_error& e) {
				lg::log_to_chat() << "Formula error in side filter: " << e.type << " at " << e.filename << ':' << e.line << ")\n";
				ERR_WML << "Formula error in side filter: " << e.type << " at " << e.filename << ':' << e.line << ")";
				return false;
			}
		}
	);

	create_attribute(literal["lua_function"],
		[](const config::attribute_value& c) { return c.str(); },
		[](const std::string& lua_function, const side_filter_args& args)
		{
			if (!lua_function.empty() && args.fc->get_lua_kernel()) {
				return args.fc->get_lua_kernel()->run_filter(lua_function.c_str(), args.t);
			}
			return true;
		}
	);

	for(auto child : cfg.all_ordered()) {
		auto cond = conditional_type::get_enum(child.first);
		if(cond) {
			cond_children_.emplace_back(std::piecewise_construct_t(), std::tuple(*cond), std::tuple(child.second, ""));
		}
		else if (child.first == "has_unit") {
			create_child(child.second, [](const vconfig& c, const side_filter_args& args) {
				unit_filter ufilter(c.make_safe());
				ufilter.set_use_flat_tod(args.use_flat_tod);

				bool found = false;
				for(const unit &u : args.fc->get_disp_context().units()) {
					if (u.side() != args.t.side()) {
						continue;
					}
					if (ufilter.matches(u)) {
						found = true;
						break;
					}
				}
				if(!found && c["search_recall_list"].to_bool(false)) {
					for(const unit_const_ptr u : args.t.recall_list()) {
						scoped_recall_unit this_unit("this_unit", args.t.save_id_or_number(), args.t.recall_list().find_index(u->id()));
						if(ufilter.matches(*u)) {
							found = true;
							break;
						}
					}
				}
				return found;
			});
		}
		else if (child.first == "enemy_of") {
			create_child(child.second, [](const vconfig& c, const side_filter_args& args) {
				side_filter enemy_filter(c, args.fc, args.use_flat_tod);
				const std::vector<int>& teams = enemy_filter.get_teams();
				if(teams.empty()) return false;
				for(const int side : teams) {
					if(!args.fc->get_disp_context().get_team(side).is_enemy(args.t.side()))
						return false;
				}
				return true;
			});
		}
		else if (child.first == "allied_with") {
			create_child(child.second, [](const vconfig& c, const side_filter_args& args) {
				side_filter allied_filter(c, args.fc, args.use_flat_tod);
				const std::vector<int>& teams = allied_filter.get_teams();
				if(teams.empty()) return false;
				for(const int side : teams) {
					if(args.fc->get_disp_context().get_team(side).is_enemy(args.t.side()))
						return false;
				}
				return true;
			});
		}
		else if (child.first == "has_enemy") {
			create_child(child.second, [](const vconfig& c, const side_filter_args& args) {
				side_filter has_enemy_filter(c, args.fc, args.use_flat_tod);
				const std::vector<int>& teams = has_enemy_filter.get_teams();
				for(const int side : teams) {
					if(args.fc->get_disp_context().get_team(side).is_enemy(args.t.side()))
					{
						return true;
					}
				}
				return false;
			});
		}
		else if (child.first == "has_ally") {
			create_child(child.second, [](const vconfig& c, const side_filter_args& args) {
				side_filter has_ally_filter(c, args.fc, args.use_flat_tod);
				const std::vector<int>& teams = has_ally_filter.get_teams();
				for(const int side : teams) {
					if(!args.fc->get_disp_context().get_team(side).is_enemy(args.t.side()))
					{
						return true;
					}
				}
				return false;
			});
		}
	}
}
