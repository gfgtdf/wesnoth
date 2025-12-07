/*
	Copyright (C) 2025
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

#include "map/location.hpp"
#include "units/ptr.hpp"
#include "units/race.hpp" // for unit_race::GENDER

#include <vector>

class config;

using ability_vector = std::vector<ability_ptr>;

class unit_ability_t
{
public:

	enum class active_on_t { offense, defense, both };
	enum class apply_to_t { self, opponent, attacker, defender, both };
	enum class affects_allies_t { yes, no, same_side_only };

	enum class affects_t { SELF = 1, OTHER = 2, EITHER = 3 };

	unit_ability_t(std::string tag, config cfg, bool inside_attack);

	static ability_ptr create(std::string tag, config cfg, bool inside_attack) {
		return std::make_shared<unit_ability_t>(tag, cfg, inside_attack);
	}

	static void do_compat_fixes(config& cfg, const std::string& tag, bool inside_attack);

	const std::string& tag() const { return tag_; };
	const std::string& id() const { return id_; };
	bool in_specials_tag() const { return in_specials_tag_; };
	const config& cfg() const { return cfg_; };

	active_on_t active_on() const { return active_on_; };
	apply_to_t apply_to() const { return apply_to_; };
	double priority() const { return priority_; };

	//has no effect in [specials]
	affects_allies_t affects_allies() const { return affects_allies_; }
	//has no effect in [specials]
	bool affects_self() const { return affects_self_; }
	//has no effect in [specials]
	bool affects_enemies() const { return affects_enemies_; }

	struct tooltip_info
	{
		t_string name;
		t_string description;
		// a unique id used for help topics, generated from name and id.
		// doesn't include the "ability_" prefix.
		// TODO: maybe use cfg["unique_id"] at some point?
		std::string help_topic_id;
	};

	//Generates a unique id to be used to identify the help page for this ability.
	static std::string get_help_topic_id(const config& cfg);
	std::string get_help_topic_id() const;


	std::string get_name(bool is_inactive = false, unit_race::GENDER = unit_race::MALE) const;
	std::string get_description(bool is_inactive = false, unit_race::GENDER = unit_race::MALE) const;

	//checks whether the ability is active according to the active_on= attribute.
	bool active_on_matches(bool student_is_attacker) const;


	//checks whether the ability matches the filter specified in a [filter_special] or [filter_ability]
	bool matches_filter(const config& filter) const;
	void write(config& abilities_cfg);


	static void parse_vector(const config& abilities_cfg, ability_vector& res, bool inside_attack);
	static config vector_to_cfg(const ability_vector& abilities);
	static ability_vector cfg_to_vector(const config& abilities_cfg, bool inside_attack);


	static ability_vector filter_tag(const ability_vector& vec, const std::string& tag);

	static std::vector<std::string> get_nonempty_ids(const ability_vector& vec) const;

	static ability_vector clone(const ability_vector& vec);

	/**
	 * Substitute gettext variables in name and description of abilities and specials
	 * @param str                  The string in which the substitution is to be done
	 *
	 * @return The string `str` with all gettext variables substitutes with corresponding special properties
	 */
	std::string substitute_variables(const std::string& str) const;



	class recursion_guard
	{
	public:
		recursion_guard(const unit_ability_t& parent);
		recursion_guard(recursion_guard&&) = delete;
		recursion_guard(const recursion_guard&) = delete;
		recursion_guard() = delete;
		~recursion_guard();

		/**
		 * Returns true if a level of recursion was available at the time when guard_against_recursion()
		 * created this object.
		 */
		operator bool() const;
		const unit_ability_t* parent;
	};

	/**
	 * Tests which might otherwise cause infinite recursion should call this, check that the
	 * returned object evaluates to true, and then keep the object returned as long as the
	 * recursion might occur, similar to a reentrant mutex that's limited to a small number of
	 * reentrances.
	 *
	 * This only expects to be called in a single thread
	 */
	recursion_guard guard_against_recursion(const unit& u) const;
//	recursion_guard guard_against_recursion(const attack_type& a) const;

private:
	std::string tag_;
	std::string id_;
	// abilities/specials inside [specials] tag follow a differnt syntax than in [abilities] tags, in paricular [filter_self] inside [specials] is equivalent to [filter_student] in abilities.
	bool in_specials_tag_;
	active_on_t active_on_;
	apply_to_t apply_to_;
	affects_allies_t affects_allies_;
	bool affects_self_;
	bool affects_enemies_;
	double priority_;
	config cfg_;

	mutable bool currently_checked_;
};
