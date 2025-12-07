/*
	Copyright (C) 2006 - 2025
	by Dominic Bolin <dominic.bolin@exong.net>
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
#include "units/ability.hpp"

#include <vector>
class config;

namespace wfl {
	class map_formula_callable;
}

class active_ability_list;


using ability_vector = std::vector<ability_ptr>;

/** Data typedef for active_ability_list. */
struct active_ability
{
	active_ability(const ability_ptr& p_ability, map_location student_loc, map_location teacher_loc)
		: student_loc(student_loc)
		, teacher_loc(teacher_loc)
		, p_ability_(p_ability)
	{
	}

	/**
	 * Used by the formula in the ability.
	 * The REAL location of the student (not the 'we are assuming the student is at this position' location)
	 * once active_ability_list can contain abilities from different 'students', as it contains abilities from
	 * a unit aswell from its opponents (abilities with apply_to= opponent)
	 */
	map_location student_loc;
	/**
	 * The location of the teacher, that is the unit who owns the ability tags
	 * (different from student because of [affect_adjacent])
	 */
	map_location teacher_loc;

	const config& ability_cfg() const { return p_ability_->cfg(); }
	const unit_ability_t& ability() const { return *p_ability_; }
private:
	/** The contents of the ability tag, never nullptr. */
	const_ability_ptr p_ability_;
};

class active_ability_list
{
public:
	explicit active_ability_list(const map_location& loc = map_location()) : cfgs_(), loc_(loc) {}

	// Implemented in unit_abilities.cpp
	std::pair<int, map_location> highest(const std::string& key, int def = 0) const
	{
		return get_extremum(key, def, std::less<int>());
	}
	std::pair<int, map_location> lowest(const std::string& key, int def = 0) const
	{
		return get_extremum(key, def, std::greater<int>());
	}

	template<typename TComp>
	std::pair<int, map_location> get_extremum(const std::string& key, int def, const TComp& comp) const;

	// The following make this class usable with standard library algorithms and such
	typedef std::vector<active_ability>::iterator       iterator;
	typedef std::vector<active_ability>::const_iterator const_iterator;

	iterator       begin() { return cfgs_.begin(); }
	const_iterator begin() const { return cfgs_.begin(); }
	iterator       end() { return cfgs_.end(); }
	const_iterator end()   const { return cfgs_.end(); }

	// Vector access
	bool                empty() const { return cfgs_.empty(); }
	active_ability& front() { return cfgs_.front(); }
	const active_ability& front() const { return cfgs_.front(); }
	active_ability& back() { return cfgs_.back(); }
	const active_ability& back()  const { return cfgs_.back(); }
	std::size_t         size() { return cfgs_.size(); }

	iterator erase(const iterator& erase_it) { return cfgs_.erase(erase_it); }
	iterator erase(const iterator& first, const iterator& last) { return cfgs_.erase(first, last); }

	template<typename... T>
	void emplace_back(T&&... args) { cfgs_.emplace_back(args...); }

	const map_location& loc() const { return loc_; }

	/** Appends the abilities from @a other to @a this, ignores other.loc() */
	void append(const active_ability_list& other)
	{
		std::copy(other.begin(), other.end(), std::back_inserter(cfgs_));
	}

	/**
	 * Appends any abilities from @a other for which the given condition returns true to @a this, ignores other.loc().
	 *
	 * @param other where to copy the elements from
	 * @param predicate a single-argument function that takes a reference to an element and returns a bool
	 */
	template<typename Predicate>
	void append_if(const active_ability_list& other, const Predicate& predicate)
	{
		std::copy_if(other.begin(), other.end(), std::back_inserter(cfgs_), predicate);
	}

private:
	// Data
	std::vector<active_ability> cfgs_;
	map_location loc_;
};

class specials_context_t {

public:
	struct specials_combatant {
		unit_const_ptr un;
		map_location loc;
		const_attack_ptr at;
	};

	specials_context_t(specials_context_t&&) = delete;
	specials_context_t(const specials_context_t&) = delete;

	specials_context_t(specials_combatant&& att, specials_combatant&& def);
	~specials_context_t();

	static specials_context_t make(specials_combatant&& self, specials_combatant&& other, bool attacking)
	{
		return specials_context_t{
			{
				std::move(attacking ? self : other),
			} , {
				std::move(attacking ? other : self),
			}
		};
	}

	void set_for_listing(bool for_listing)
	{
		is_for_listing = for_listing;
	}

	struct self_and_other_ref {
		const specials_combatant& self;
		const specials_combatant& other;
	};

	self_and_other_ref self_and_other(const attack_type& self_att) const
	{
		return &self_att == attacker.at.get() ? self_and_other_ref{ attacker, defender } : self_and_other_ref{ defender, attacker };
	}
	self_and_other_ref self_and_other(const unit& self_un) const
	{
		return &self_un == attacker.un.get() ? self_and_other_ref{ attacker, defender } : self_and_other_ref{ defender, attacker };
	}

	const specials_combatant& other(const specials_combatant& self) const
	{
		return &self == &attacker ? defender : attacker;
	}

	specials_combatant attacker;
	specials_combatant defender;
	bool is_for_listing;

	active_ability_list get_active_specials(const attack_type& at, const std::string& tag) const;

	active_ability_list get_abilities_weapons(const std::string& tag, const unit& un) const;

	bool has_active_special(const attack_type& at, const std::string& tag) const;
	bool has_active_special_id(const attack_type& at, const std::string& id) const;
	bool has_active_special_matching_filter(const attack_type& at, const config& filter) const;

	static bool has_active_ability_id(const unit& un, map_location loc, const std::string& id);
	static bool has_active_ability_matching_filter(const unit& un, map_location loc, const config& filter);

	bool is_special_active(const specials_combatant& wep, const unit_ability_t& ab, unit_ability_t::affects_t whom) const;

	void add_formula_context(wfl::map_formula_callable& callable) const;

	std::vector<unit_ability_t::tooltip_info> special_tooltips(const attack_type& at, boost::dynamic_bitset<>& active_list) const;
	std::vector<unit_ability_t::tooltip_info> abilities_special_tooltips(const attack_type& at, boost::dynamic_bitset<>& active_list) const;

	std::string describe_weapon_specials(const attack_type& at) const;
	std::string describe_weapon_specials_value(const attack_type& at, const std::set<std::string>& checking_tags) const;

	active_ability_list get_active_combat_teachers(const attack_type& at) const;

};

namespace unit_abilities
{
bool filter_base_matches(const config& cfg, int def);

enum value_modifier {NOT_USED,SET,ADD,MUL,DIV};

enum EFFECTS { EFFECT_DEFAULT=1, EFFECT_CUMULABLE=2, EFFECT_WITHOUT_CLAMP_MIN_MAX=3 };

struct individual_effect
{
	void set(value_modifier t, int val, const config& abil,const map_location &l);
	value_modifier type{NOT_USED};
	int value{0};
	const config* ability{nullptr};
	map_location loc{map_location::null_location()};
};

class effect
{
	public:
		effect(const active_ability_list& list, int def, const specials_context_t* ctx = nullptr, EFFECTS wham = EFFECT_DEFAULT);
		// Provide read-only access to the effect list:
		typedef std::vector<individual_effect>::const_iterator iterator;
		typedef std::vector<individual_effect>::const_iterator const_iterator;

		int get_composite_value() const
		{ return composite_value_; }
		double get_composite_double_value() const
		{ return composite_double_value_; }
		const_iterator begin() const
		{ return effect_list_.begin(); }
		const_iterator end() const
		{ return effect_list_.end(); }
	private:
		/** Part of the constructor, calculates for a group of abilities with equal priority. */
		void effect_impl(const active_ability_list& list, int def, const specials_context_t* ctx, EFFECTS wham);
		std::vector<individual_effect> effect_list_;
		int composite_value_;
		double composite_double_value_;
};


}
