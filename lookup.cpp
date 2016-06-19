#include "lookup.h"
#include "main.h"
#include "utility.h"

#include <boost/filesystem.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <cassert>
#include <experimental/string_view>
#include <fstream>
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
static struct Set_Adapter<T> make_set_adapter(std::vector<T> &v) {
	return {v};
}

struct File_content_iterator {
	File_content_iterator(const int *ip)
		: ip(ip) {}
	std::string operator*() {
		assert(ip);
		std::string retval;
		file.seekg(*ip);
		if (!std::getline(file, retval, file_separator)) {
			throw std::runtime_error("Failed reading file");
		}
		return retval;
	}
	Symbol_lib_entry get_element() {
		std::string retval;
		file.seekg(*ip);
		if (!std::getline(file, retval, entry_separator)) {
			throw std::runtime_error("Failed reading file");
		}
		return {std::move(retval)};
	}
	File_content_iterator &operator++() {
		++ip;
		return *this;
	}
	File_content_iterator &operator+=(long int offset) {
		ip += offset;
		return *this;
	}

	static std::ifstream file;
	const int *ip = nullptr;
};

namespace std {
	template <>
	struct iterator_traits<File_content_iterator> {
		using iterator_category = random_access_iterator_tag;
		using value_type = std::string;
		using difference_type = std::iterator_traits<int *>::difference_type;
	};
}

std::ifstream File_content_iterator::file;

bool operator<(const File_content_iterator &lhs, const File_content_iterator &rhs) {
	return lhs.ip < rhs.ip;
}

auto operator-(const File_content_iterator &lhs, const File_content_iterator &rhs) {
	return lhs.ip - rhs.ip;
}

template <class Function>
struct RAII {
	RAII(Function &&f)
		: f(std::move(f)) {}
	~RAII() {
		std::move(f)();
	}

	private:
	Function f;
};

template <class T>
using remove_cvr = std::remove_cv_t<std::remove_reference_t<T>>;

template <class Function>
RAII<Function> create_RAII(Function &&f) {
	return RAII<remove_cvr<Function>>(std::forward<Function>(f));
}
#define ON_SCOPE_EXIT_CAT(a, b) ON_SCOPE_EXIT_CAT_(a, b) // force expand
#define ON_SCOPE_EXIT_CAT_(a, b) a##b                    // actually concatenate
#define ON_SCOPE_EXIT(CODE) auto ON_SCOPE_EXIT_CAT(ON_SCOPE_EXIT_, __LINE__) = create_RAII([&]() { CODE })

enum class Search_type { exact, prefix };

static std::vector<Symbol_lib_entry> lookup(string_view symbol, Search_type st) {
	std::vector<int> indexes;
	{
		int index_size = boost::filesystem::file_size(index_filepath);
		indexes.resize(index_size / sizeof(int));
		std::ifstream index_file(index_filepath, std::ios_base::in | std::ios::binary);
		index_file.read(any_cast<char *>(indexes.data()), index_size);
		assert(index_file);
		if (!index_file) {
			throw std::runtime_error("Failed reading index file");
		}
	}
	File_content_iterator::file.open(data_base_filepath, std::ios_base::in | std::ios::binary);
	ON_SCOPE_EXIT(File_content_iterator::file.close(););

	//TODO: replace lower_bound with binary_interpolation_search
	auto index_begin = indexes.data();
	auto index_end = indexes.data() + indexes.size();
	auto pos = std::lower_bound(File_content_iterator{index_begin}, File_content_iterator{index_end}, symbol);
	if (pos.ip == index_end) {
		return {};
	};
	auto value = pos.get_element();
	if (st == Search_type::exact) {
		if (value.get_symbol() == symbol) {
			return {std::move(value)};
		};
		return {};
	}
	std::vector<Symbol_lib_entry> retval;
	while (value.get_symbol().find(symbol) == 0) {
		retval.push_back(std::move(value));
		++pos;
		if (pos.ip == index_end) {
			return retval;
		}
		value = pos.get_element();
	}
	return retval;
}

std::vector<std::string> exact_lookup(string_view symbol) {
	auto symbols = lookup(symbol, Search_type::exact);
	if (symbols.empty()) {
		return {};
	};
	assert(symbols.size() == 1);
	std::vector<std::string> retval;
	const auto &libs = symbols.front().get_libs_view();
	retval.reserve(libs.size());
	//std::copy(std::begin(libs), std::end(libs), std::back_inserter(retval));
	for (auto &lib : libs) {
		retval.emplace_back(lib.data(), lib.size());
	}
	return retval;
}

std::vector<Symbol_lib_entry> prefix_lookup(string_view symbol) {
	return lookup(symbol, Search_type::prefix);
}
