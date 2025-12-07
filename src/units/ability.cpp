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

/**
 *  @file
 *  Manage unit-abilities, like heal, cure, and weapon_specials.
 */

#include "deprecation.hpp"
#include "display.hpp"
#include "display_context.hpp"
#include "filter_context.hpp"
#include "font/standard_colors.hpp"
#include "formula/callable_objects.hpp"
#include "formula/formula.hpp"
#include "formula/function_gamestate.hpp"
#include "formula/string_utils.hpp"
#include "game_board.hpp"
#include "game_version.hpp" // for version_info
#include "gettext.hpp"
#include "language.hpp"
#include "lexical_cast.hpp"
#include "log.hpp"
#include "map/map.hpp"
#include "resources.hpp"
#include "serialization/markup.hpp"
#include "serialization/string_utils.hpp"
#include "team.hpp"
#include "terrain/filter.hpp"
#include "units/types.hpp"
#include "units/abilities.hpp"
#include "units/ability_tags.hpp"
#include "units/filter.hpp"
#include "units/map.hpp"
#include "utils/config_filters.hpp"
#include "units/filter.hpp"
#include "units/unit.hpp"

#include <utility>



static lg::log_domain log_engine("engine");
#define ERR_NG LOG_STREAM(err, log_engine)

static lg::log_domain log_wml("wml");
#define ERR_WML LOG_STREAM(err, log_wml)

namespace
{
	using namespace std::string_literals;
	const std::array numeric_keys{
		"value"s, "add"s, "sub"s, "multiply"s, "divide"s, "max_value"s, "min_value"s
	};
}


/*
 *
 * [abilities]
 * ...
 *
 * [heals]
 *	value=4
 *	max_value=8
 *	cumulative=no
 *	affect_allies=yes
 *	name= _ "heals"
 *	female_name= _ "female^heals"
 *	name_inactive=null
 *	female_name_inactive=null
 *	description=  _ "Heals:
Allows the unit to heal adjacent friendly units at the beginning of each turn.

A unit cared for by a healer may heal up to 4 HP per turn.
A poisoned unit cannot be cured of its poison by a healer, and must seek the care of a village or a unit that can cure."
 *	description_inactive=null
 *
 *	affect_self=yes
 *	[filter] // SUF
 *		...
 *	[/filter]
 *	[filter_self] // SUF
 *		...
 *	[/filter_self]
 *	[filter_adjacent] // SUF
 *		adjacent=n,ne,nw
 *		...
 *	[/filter_adjacent]
 *	[filter_adjacent_location]
 *		adjacent=n,ne,nw
 *		...
 *	[/filter_adjacent]
 *	[affect_adjacent]
 *		adjacent=n,ne,nw
 *		[filter] // SUF
 *			...
 *		[/filter]
 *	[/affect_adjacent]
 *	[affect_adjacent]
 *		adjacent=s,se,sw
 *		[filter] // SUF
 *			...
 *		[/filter]
 *	[/affect_adjacent]
 *
 * [/heals]
 *
 * ...
 * [/abilities]
 *
 */

unit_ability_t::unit_ability_t(std::string tag, config cfg, bool inside_attack)
	: tag_(std::move(tag))
	, id_(cfg["id"].str())
	, in_specials_tag_(inside_attack)
	, active_on_(active_on_t::both)
	, apply_to_(apply_to_t::self)
	, affects_allies_(affects_allies_t::same_side_only)
	, affects_self_(true)
	, affects_enemies_(false)
	, priority_(cfg["priority"].to_double(0.00))
	, cfg_(std::move(cfg))
	, currently_checked_(false)
{
	do_compat_fixes(cfg_, tag_, inside_attack);

	if (tag_ != "resistance" && tag_ != "leadership") {
		std::string apply_to = cfg_["apply_to"].str();
		apply_to_ = apply_to == "attacker" ? apply_to_t::attacker :
			apply_to == "defender" ? apply_to_t::defender :
			apply_to == "self" ? apply_to_t::self :
			apply_to == "opponent" ? apply_to_t::opponent :
			apply_to == "both" ? apply_to_t::both :
			apply_to_t::self;

	}
	if (tag_ != "leadership") {
		std::string active_on = cfg_["active_on"].str();
		active_on_ = active_on == "defense" ? active_on_t::defense :
			active_on == "offense" ? active_on_t::offense :
			active_on_t::both;
	}
	if (!cfg_.has_child("affect_adjacent")) {
		//optimisation
		affects_allies_ = affects_allies_t::no;
	}
	if (cfg_["affect_allies"].to_bool(false)) {
		affects_allies_ = affects_allies_t::yes;
	}
	if (!cfg_["affect_allies"].to_bool(true)) {
		affects_allies_ = affects_allies_t::no;
	}
	affects_self_ = cfg_["affect_self"].to_bool(true);
	affects_enemies_ = cfg_["affect_enemies"].to_bool(false);
}

void unit_ability_t::do_compat_fixes(config& cfg, const std::string& tag, bool inside_attack)
{
	// replace deprecated backstab with formula
	if (!cfg["backstab"].blank()) {
		deprecated_message("backstab= in weapon specials", DEP_LEVEL::INDEFINITE, "", "Use [filter_opponent] with a formula instead; the code can be found in data/core/macros/ in the WEAPON_SPECIAL_BACKSTAB macro.");
	}
	if (cfg["backstab"].to_bool()) {
		const std::string& backstab_formula = "enemy_of(self, flanker) and not flanker.petrified where flanker = unit_at(direction_from(loc, other.facing))";
		config& filter_opponent = cfg.child_or_add("filter_opponent");
		config& filter_opponent2 = filter_opponent.empty() ? filter_opponent : filter_opponent.add_child("and");
		filter_opponent2["formula"] = backstab_formula;
	}
	cfg.remove_attribute("backstab");

	// replace deprecated filter_adjacent/filter_adjacent_location with formula
	std::string filter_teacher = inside_attack ? "filter_self" : "filter";
	if (cfg.has_child("filter_adjacent")) {
		if (inside_attack) {
			deprecated_message("[filter_adjacent]in weapon specials in [specials] tags", DEP_LEVEL::INDEFINITE, "", "Use [filter_self][filter_adjacent] instead.");
		}
		else {
			deprecated_message("[filter_adjacent] in abilities", DEP_LEVEL::INDEFINITE, "", "Use [filter][filter_adjacent] instead or other unit filter.");
		}
	}
	if (cfg.has_child("filter_adjacent_location")) {
		if (inside_attack) {
			deprecated_message("[filter_adjacent_location]in weapon specials in [specials] tags", DEP_LEVEL::INDEFINITE, "", "Use [filter_self][filter_location][filter_adjacent_location] instead.");
		}
		else {
			deprecated_message("[filter_adjacent_location] in abilities", DEP_LEVEL::INDEFINITE, "", "Use [filter][filter_location][filter_adjacent_location] instead.");
		}
	}

	//These tags are were never supported inside [specials] according to the wiki.
	for (config& filter_adjacent : cfg.child_range("filter_adjacent")) {
		if (filter_adjacent["count"].empty()) {
			//Previously count= behaved differenty in abilities.cpp and in filter.cpp according to the wiki
			deprecated_message("omitting count= in [filter_adjacent] in abilities", DEP_LEVEL::FOR_REMOVAL, version_info("1.21"), "specify count explicitly");
			filter_adjacent["count"] = map_location::parse_directions(filter_adjacent["adjacent"]).size();
		}
		cfg.child_or_add(filter_teacher).add_child("filter_adjacent", filter_adjacent);
	}
	cfg.remove_children("filter_adjacent");
	for (config& filter_adjacent : cfg.child_range("filter_adjacent_location")) {
		if (filter_adjacent["count"].empty()) {
			//Previously count= bahves differenty in abilities.cpp and in filter.cpp according to the wiki
			deprecated_message("omitting count= in [filter_adjacent_location] in abilities", DEP_LEVEL::FOR_REMOVAL, version_info("1.21"), "specify count explicitly");
			filter_adjacent["count"] = map_location::parse_directions(filter_adjacent["adjacent"]).size();
		}
		cfg.child_or_add(filter_teacher).add_child("filter_location").add_child("filter_adjacent_location", filter_adjacent);
	}
	cfg.remove_children("filter_adjacent_location");

	if (tag == "resistance" || tag == "leadership") {
		if (auto child = cfg.optional_child("filter_second_weapon")) {
			cfg.add_child("filter_opponent").add_child("filter_weapon", *child);
		}
		if (auto child = cfg.optional_child("filter_weapon")) {
			cfg.add_child("filter_student").add_child("filter_weapon", *child);
		}
		cfg.remove_children("filter_second_weapon");
		cfg.remove_children("filter_weapon");
	}
}


std::string unit_ability_t::get_help_topic_id(const config& cfg)
{
	// NOTE: neither ability names nor ability ids are necessarily unique. Creating
	// topics for either each unique name or each unique id means certain abilities
	// will be excluded from help. So... the ability topic ref id is a combination
	// of id and (untranslated) name. It's rather ugly, but it works.
	return cfg["id"].str() + cfg["name"].t_str().base_str();
}

std::string unit_ability_t::get_help_topic_id() const
{
	return id() + cfg()["name"].t_str().base_str();
}


void unit_ability_t::parse_vector(const config& abilities_cfg, ability_vector& res, bool inside_attack)
{
	for (auto item : abilities_cfg.all_children_range()) {
		res.push_back(unit_ability_t::create(item.key, item.cfg, inside_attack));
	}
}

ability_vector unit_ability_t::cfg_to_vector(const config& abilities_cfg, bool inside_attack)
{
	ability_vector res;
	parse_vector(abilities_cfg, res, inside_attack);
	return res;
}

ability_vector unit_ability_t::filter_tag(const ability_vector& abs, const std::string& tag)
{
	ability_vector res;
	for (const ability_ptr& p_ab : abs) {
		if (p_ab->tag() == tag) {
			res.push_back(p_ab);
		}
	}
	return res;
}

ability_vector unit_ability_t::clone(const ability_vector& abs)
{
	ability_vector res;
	for (const ability_ptr& p_ab : abs) {
		res.push_back(std::make_shared<unit_ability_t>(*p_ab));
	}
	return res;
}

config unit_ability_t::vector_to_cfg(const ability_vector& abilities)
{
	config abilities_cfg;
	for (const auto& item : abilities) {
		item->write(abilities_cfg);
	}
	return abilities_cfg;
}


void unit_ability_t::write(config& abilities_cfg)
{
	abilities_cfg.add_child(tag(), cfg());
}

std::string unit_ability_t::substitute_variables(const std::string& str) const {
	// TODO add more [specials] keys

	utils::string_map symbols;

	// [plague]type= -> $type
	if(tag() == "plague") {
		// Substitute [plague]type= as $type
		const auto iter = unit_types.types().find(cfg()["type"]);

		// TODO: warn if an invalid type is specified?
		if (iter == unit_types.types().end()) {
			return str;
		}

		const unit_type& type = iter->second;
		symbols.emplace("type", type.type_name());
	}

	// weapon specials with value keys, like value, add, sub etc.
	// i.e., [heals]value= -> $value, [regenerates]add= -> $add etc.
	for(const auto& vkey : numeric_keys) {
		if(cfg().has_attribute(vkey)) {
			if(vkey == "multiply" || vkey == "divide") {
				const std::string lang_locale = get_language().localename;
				std::stringstream formatter_str;
				try {
					formatter_str.imbue(std::locale{lang_locale});
				} catch(const std::runtime_error&) {}
				formatter_str << cfg()[vkey].to_double();
				symbols.emplace(vkey, formatter_str.str());
			} else {
				symbols.emplace(vkey, std::to_string(cfg()[vkey].to_int()));
			}
		}
	}

	return symbols.empty() ? str : utils::interpolate_variables_into_string(str, &symbols);
}


namespace {
	const config_attribute_value& get_attr_four_fallback(const config& cfg, bool b1, bool b2, std::string_view s_yes_yes, std::string_view s_yes_no, std::string_view s_no_yes, std::string_view s_no_no)
	{
		if (b1 && b2) {
			if (auto* attr = cfg.get(s_yes_yes)) { return *attr; }
		}
		if (b1) {
			if (auto* attr = cfg.get(s_yes_no)) { return *attr; }
		}
		if (b2) {
			if (auto* attr = cfg.get(s_no_yes)) { return *attr; }
		}
		return cfg[s_no_no];
	}
}

std::string unit_ability_t::get_name(bool is_inactive, unit_race::GENDER gender) const
{
	bool is_female = gender == unit_race::FEMALE;
	std::string res = get_attr_four_fallback(cfg_, is_inactive, is_female, "female_name_inactive", "name_inactive", "female_name", "name").str();
	return substitute_variables(res);
}

std::string unit_ability_t::get_description(bool is_inactive, unit_race::GENDER gender) const
{
	bool is_female = gender == unit_race::FEMALE;
	std::string res = get_attr_four_fallback(cfg_, is_inactive, is_female, "female_description_inactive", "description_inactive", "female_description", "description").str();
	return substitute_variables(res);
}

bool unit_ability_t::active_on_matches(bool student_is_attacker) const
{
	if (active_on() == unit_ability_t::active_on_t::both) {
		return true;
	}
	if (active_on() == unit_ability_t::active_on_t::offense && student_is_attacker) {
		return true;
	}
	if (active_on() == unit_ability_t::active_on_t::defense && !student_is_attacker) {
		return true;
	}
	return false;
}


unit_ability_t::recursion_guard::recursion_guard(const unit_ability_t& p)
	: parent()
{
	if (!p.currently_checked_) {
		p.currently_checked_ = true;
		parent = &p;
	}
}

unit_ability_t::recursion_guard::~recursion_guard()
{
	if (parent) {
		parent->currently_checked_ = false;
	}
}

unit_ability_t::recursion_guard::operator bool() const {
	return bool(parent);
}

unit_ability_t::recursion_guard unit_ability_t::guard_against_recursion(const unit& u) const
{
	if (currently_checked_) {
		static std::vector<std::tuple<std::string, std::string>> already_shown;

		auto identifier = std::tuple<std::string, std::string>{ u.id(), cfg().debug()};
		if (!utils::contains(already_shown, identifier)) {

			std::string_view filter_text_view = std::get<1>(identifier);
			utils::trim(filter_text_view);
			ERR_NG << "Looped recursion error for unit '" << u.id()
				<< "' while checking ability '" << filter_text_view << "'";

			// Arbitrary limit, just ensuring that having a huge number of specials causing recursion
			// warnings can't lead to unbounded memory consumption here.
			if (already_shown.size() > 100) {
				already_shown.clear();
			}
			already_shown.push_back(std::move(identifier));
		}
	}
	return recursion_guard(*this);
}

namespace
{
	bool exclude_ability_attributes(const std::string& tag_name, const config & filter)
	{
		///check what filter attributes used can be used in type of ability checked.
		bool abilities_check = abilities_list::ability_value_tags().count(tag_name) != 0 || abilities_list::ability_no_value_tags().count(tag_name) != 0;
		if(filter.has_attribute("active_on") && tag_name != "resistance" && abilities_check)
			return false;
		if(filter.has_attribute("apply_to")  && tag_name != "resistance" && abilities_check)
			return false;

		if(filter.has_attribute("overwrite_specials") && abilities_list::weapon_math_tags().count(tag_name) == 0)
			return false;

		bool no_value_weapon_abilities_check =  abilities_list::no_weapon_math_tags().count(tag_name) != 0 || abilities_list::ability_no_value_tags().count(tag_name) != 0;
		if(filter.has_attribute("cumulative") && no_value_weapon_abilities_check && (tag_name != "swarm" || tag_name != "berserk"))
			return false;
		if(filter.has_attribute("value") && (no_value_weapon_abilities_check && tag_name != "berserk"))
			return false;
		if(filter.has_attribute("add") && no_value_weapon_abilities_check)
			return false;
		if(filter.has_attribute("sub") && no_value_weapon_abilities_check)
			return false;
		if(filter.has_attribute("multiply") && no_value_weapon_abilities_check)
			return false;
		if(filter.has_attribute("divide") && no_value_weapon_abilities_check)
			return false;
		if(filter.has_attribute("priority") && no_value_weapon_abilities_check)
			return false;

		bool all_engine =  abilities_list::no_weapon_math_tags().count(tag_name) != 0 || abilities_list::weapon_math_tags().count(tag_name) != 0 || abilities_list::ability_value_tags().count(tag_name) != 0 || abilities_list::ability_no_value_tags().count(tag_name) != 0;
		if(filter.has_attribute("replacement_type") && tag_name != "damage_type" && all_engine)
			return false;
		if(filter.has_attribute("alternative_type") && tag_name != "damage_type" && all_engine)
			return false;
		if(filter.has_attribute("type") && tag_name != "plague" && all_engine)
			return false;

		return true;
	}

	bool matches_ability_filter(const config & cfg, const std::string& tag_name, const config & filter)
	{
		using namespace utils::config_filters;

		//check if attributes have right to be in type of ability checked
		if(!exclude_ability_attributes(tag_name, filter))
			return false;

		// tag_name and id are equivalent of ability ability_type and ability_id/type_active filters
		//can be extent to special_id/type_active. If tag_name or id matche if present in list.
		const std::vector<std::string> filter_type = utils::split(filter["tag_name"]);
		if(!filter_type.empty() && !utils::contains(filter_type, tag_name))
			return false;

		if(!string_matches_if_present(filter, cfg, "id", ""))
			return false;

		//when affect_adjacent=yes detect presence of [affect_adjacent] in abilities, if no
		//then matches when tag not present.
		if(!filter["affect_adjacent"].empty()){
			bool adjacent = cfg.has_child("affect_adjacent");
			if(filter["affect_adjacent"].to_bool() != adjacent){
				return false;
			}
		}

		//these attributs below filter attribute used in all engine abilities.
		//matches if filter attribute have same boolean value what attribute
		if(!bool_matches_if_present(filter, cfg, "affect_self", true))
			return false;

		//here if value of affect_allies but also his presence who is checked because
		//when affect_allies not specified, ability affect unit of same side what owner only.
		if(!bool_or_empty(filter, cfg, "affect_allies"))
			return false;

		if(!bool_matches_if_present(filter, cfg, "affect_enemies", false))
			return false;


		//cumulative, overwrite_specials and active_on check attributes used in all abilities
		//who return a numerical value.
		if(!bool_matches_if_present(filter, cfg, "cumulative", false))
			return false;

		if(!string_matches_if_present(filter, cfg, "overwrite_specials", "none"))
			return false;

		if(!string_matches_if_present(filter, cfg, "active_on", "both"))
			return false;

		if(abilities_list::weapon_math_tags().count(tag_name) != 0 || abilities_list::ability_value_tags().count(tag_name) != 0) {
			if(!double_matches_if_present(filter, cfg, "priority", 0.00)) {
				return false;
			}
		} else {
			if(!double_matches_if_present(filter, cfg, "priority")) {
				return false;
			}
		}

		//value, add, sub multiply and divide check values of attribute used in engines abilities(default value of 'value' can be checked when not specified)
		//who return numericals value but can also check in non-engine abilities(in last case if 'value' not specified none value can matches)
		if(!filter["value"].empty()){
			if(tag_name == "drains"){
				if(!int_matches_if_present(filter, cfg, "value", 50)){
					return false;
				}
			} else if(tag_name == "berserk"){
				if(!int_matches_if_present(filter, cfg, "value", 1)){
					return false;
				}
			} else if(tag_name == "heal_on_hit" || tag_name == "heals" || tag_name == "regenerate" || tag_name == "leadership"){
				if(!int_matches_if_present(filter, cfg, "value" , 0)){
					return false;
				}
			} else {
				if(!int_matches_if_present(filter, cfg, "value")){
					return false;
				}
			}
		}

		if(!int_matches_if_present_or_negative(filter, cfg, "add", "sub"))
			return false;

		if(!int_matches_if_present_or_negative(filter, cfg, "sub", "add"))
			return false;

		if(!double_matches_if_present(filter, cfg, "multiply"))
			return false;

		if(!double_matches_if_present(filter, cfg, "divide"))
			return false;


		//apply_to is a special case, in resistance ability, it check a list of damage type used by [resistance]
		//but in weapon specials, check identity of unit affected by special(self, opponent tc...)
		if(tag_name == "resistance"){
			if(!set_includes_if_present(filter, cfg, "apply_to")){
				return false;
			}
		} else {
			if(!string_matches_if_present(filter, cfg, "apply_to", "self")){
				return false;
			}
		}

		//the three attribute below are used for check in specifics abilitie:
		//replacement_type and alternative_type are present in [damage_type] only for engine abilities
		//and type for [plague], but if someone want use this in non-engine abilities, these attribute can be checked outside type mentioned.
		//

		//for damage_type only(in engine cases)
		if(!string_matches_if_present(filter, cfg, "replacement_type", ""))
			return false;

		if(!string_matches_if_present(filter, cfg, "alternative_type", ""))
			return false;

		//for plague only(in engine cases)
		if(!string_matches_if_present(filter, cfg, "type", ""))
			return false;

		//the wml_filter is used in cases where the attribute we are looking for is not
		//previously listed or to check the contents of the sub_tags ([filter_adjacent],[filter_self],[filter_opponent] etc.
		//If the checked set does not exactly match the content of the capability, the function returns a false response.
		auto fwml = filter.optional_child("filter_wml");
		if (fwml){
			if(!cfg.matches(*fwml)){
				return false;
			}
		}

		// Passed all tests.
		return true;
	}

	static bool common_matches_filter(const config & cfg, const std::string& tag_name, const config & filter)
	{
		// Handle the basic filter.
		bool matches = matches_ability_filter(cfg, tag_name, filter);

		// Handle [and], [or], and [not] with in-order precedence
		for(const auto [key, condition_cfg] : filter.all_children_view() )
		{
			// Handle [and]
			if ( key == "and" )
				matches = matches && common_matches_filter(cfg, tag_name, condition_cfg);

			// Handle [or]
			else if ( key == "or" )
				matches = matches || common_matches_filter(cfg, tag_name, condition_cfg);

			// Handle [not]
			else if ( key == "not" )
				matches = matches && !common_matches_filter(cfg, tag_name, condition_cfg);
		}

		return matches;
	}
}

bool unit_ability_t::matches_filter(const config & filter) const
{
	return common_matches_filter(cfg(), tag(), filter);
}
