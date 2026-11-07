#pragma once

#include "iterable.h"

#include <string>

const std::string &get_install_status();

struct Dpkg_Packages : Iterable {
	Dpkg_Packages();
	void assign_next(std::optional<std::string_view> &next);

	private:
	std::string packages;
	std::string_view view;
};
