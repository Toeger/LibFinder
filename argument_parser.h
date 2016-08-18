#ifndef ARGUMENT_PARSER_H
#define ARGUMENT_PARSER_H

#include "gsl-lite.h"
#include "main.h"

#include <string>

namespace Argument_parser {
	struct Update{
		int jobs;
	};

	struct Lookup{
		std::string symbol;
		//todo: output format enum
	};

	enum class Argument_type : char {update, lookup, help, invalid};

	Argument_type get_argument_type(const gsl::span<string_view> args);

	Update get_update_arguments(const gsl::span<string_view> args);

	Lookup get_lookup_arguments(const gsl::span<string_view> args);
}


#endif // ARGUMENT_PARSER_H