#include "argument_parser.h"
#include "gsl-lite.h"
#include "main.h"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

enum class Argument_is_required : bool { yes, no };
enum class Argument_has_parameter : char { yes, no, maybe };

static bool is_prefix_of(string_view s1, string_view s2) {
	//this one is slightly hacky, but I can't think of a better way right now
	return string_view(s1.data(), s2.size()) == s2;
}

struct Argument_definition {
	std::string shortform; //something like -h
	std::string longform;  //something like --help
	std::string argument;  //the argument, like in -j8 or -j=8 the 8 would be the argument
	Argument_is_required is_required;
	Argument_has_parameter has_parameter;
	Argument_definition(string_view shortform, string_view longform, Argument_is_required is_required, Argument_has_parameter has_parameter,
						string_view default_argument = {})
		: shortform(shortform)
		, longform(longform)
		, argument(default_argument)
		, is_required(is_required)
		, has_parameter(has_parameter) {
		if (has_parameter == Argument_has_parameter::yes) {
			assert(!default_argument.empty());
		}
		if (has_parameter == Argument_has_parameter::no) {
			assert(default_argument.empty());
		}
	}
	bool is_fulfilled_by(const gsl::span<string_view> args) const {
		for (auto it = std::begin(args); it != std::end(args); ++it) {
			if (is_prefix_of(longform, *it)) {
				if (longform == *it) {
					//TODO: check if required arguments are present
					return true;
				}
			} else if (is_prefix_of(shortform, *it)) {
				if (shortform == *it) {
					//TODO: check if required arguments are present
					return true;
				}
			}
		}
		return false;
	}
};

static const Argument_definition help_args[] = {{"-h", "--help", Argument_is_required::yes, Argument_has_parameter::no}};
static const Argument_definition update_args[] = {{"-u", "--update", Argument_is_required::yes, Argument_has_parameter::maybe}};

static bool is_satisfied(const gsl::span<const Argument_definition> &argdefs, const gsl::span<string_view> &args) {
	return std::any_of(std::begin(argdefs), std::end(argdefs), [&args](Argument_definition arg) { return arg.is_fulfilled_by(args); });
}

Argument_parser::Argument_type Argument_parser::get_argument_type(const gsl::span<string_view> args) {
	for (auto argp : std::initializer_list<std::pair<gsl::span<const Argument_definition>, Argument_parser::Argument_type>>{
			 {help_args, Argument_parser::Argument_type::help}, {update_args, Argument_parser::Argument_type::update},
		 }) {
		if (is_satisfied(argp.first, args)) {
			return argp.second;
		}
	}
	return Argument_parser::Argument_type::invalid;
}
