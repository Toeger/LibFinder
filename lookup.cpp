#include "lookup.h"
#include "main.h"

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cassert>
#include <experimental/string_view>
#include <set>
#include <vector>

using string_view = std::experimental::string_view;

template <class T>
struct Set_Adapter {
	//insert objects into a vector while keeping it sorted
	Set_Adapter(std::vector<T> &v)
		: v(v) {}
	template <class... Args>
	void insert(Args... args) {
		T t{args...};
		auto it = std::lower_bound(std::begin(v), std::end(v), t);
		if (it == std::end(v) || *it != t) {
			v.insert(it, std::move(t));
		}
	}

	private:
	std::vector<T> &v;
};

template <class T>
struct Set_Adapter<T> make_set_adapter(std::vector<T> &v) {
	return {v};
}

const char *
symbol_lookup(string_view data, string_view symbol, Search_type st) {
	auto clean_pos = [](const char *&data) {
		while (data[-1] != entry_separator) {
			data--;
		}
	};

	auto estimate_position = [](const char *left, const char *right, const char *to_estimate) {
		while (*left == *right) {
			left++;
			right++;
			to_estimate++;
		}
		auto get_value = [](const char *p) {
			auto get_number = [](char c) -> int { return std::min(std::max(c - ' ', 0), 'z' + 0); };
			return get_number(p[0]) * 90 * 90 + get_number(p[1]) * 90 + get_number(p[2]);
		};

		auto left_value = get_value(left);
		auto right_value = get_value(right);
		auto estimate_value = get_value(to_estimate);
		assert(left_value <= estimate_value);
		assert(estimate_value <= right_value);
		if (left_value == right_value) {
			return left;
		}
		return left + (right - left) * (estimate_value - left_value) / (right_value - left_value);
	};

	auto left = data.data() + 1;
	auto right = data.data() + data.size() - 1;
	clean_pos(right);
	while (right - left > 1000) {
		auto mid = estimate_position(left, right, symbol.data());
		clean_pos(mid);
		if (symbol < mid) {
			right = mid;
		} else {
			left = mid;
		}
		//add a binary search component to prevent O(n) degradation
		mid = left + (right - left) / 2;
		clean_pos(mid);
		if (symbol < mid) {
			right = mid;
		} else {
			left = mid;
		}
	}
	while (*right++ != file_separator) {
	}
	string_view rest(left, right - left);
	auto pos = rest.find(symbol);
	if (pos == rest.npos) {
		//didn't find it
		return nullptr;
	}
	auto result = rest.data() + pos;
	if (result[symbol.size()] != file_separator) { //found a prefix
		if (st != Search_type::prefix) {
			return nullptr;
		}
	}
	return result;
}

std::vector<std::string> lookup(string_view symbol, Search_type st) {
	std::vector<std::string> retval;
	boost::interprocess::file_mapping file(data_base_filepath.c_str(), boost::interprocess::read_only);
	boost::interprocess::mapped_region region(file, boost::interprocess::read_only);
	string_view data(static_cast<const char *>(region.get_address()), region.get_size());
	auto files = symbol_lookup(data, symbol, st);
	if (files) {
		auto sva = make_set_adapter(retval);
		auto save_files = [&sva](const char *files) {
			while (*files++ != file_separator) {
			}
			auto files_end = files;
			while (*++files_end != entry_separator) {
			}
			string_view all_files(files, files_end - files);
			for (auto pos = all_files.find(file_separator); pos != all_files.npos; pos = all_files.find(file_separator)) {
				sva.insert(all_files.data(), pos);
				all_files.remove_prefix(pos + 1);
			}
			sva.insert(all_files.data(), all_files.size());
			return files_end;
		};
		switch (st) {
			case Search_type::exact: {
				save_files(files);
				break;
			}
			case Search_type::prefix: {
				do {
					files = save_files(files) + 1;
				} while (string_view(files, symbol.size()) == symbol);
				break;
			}
		}
	}
	return retval;
}
