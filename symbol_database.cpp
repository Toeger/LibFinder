#include "symbol_database.h"
#include "cast.h"
#include "color.h"
#include "profile.h"
#include "utility.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <print>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#if __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace {
	namespace For::documentation::of::file::format::only {
		constexpr auto variable = !0;	   //indicates variable value
		using Lib_Index = unsigned;		   //an index type with variable byte size lib_index_size
		using File_Offset = std::uint32_t; //4 byte file offset type, offset from start of database file

		const char magic_number[12] = "LibFinderV1";   //if it doesn't match file content, this documentation does not apply
		char install_status[] = {variable};			   //the state of the packages when the database was created, null-terminated
		const std::uint32_t symbol_count{variable};	   //number of demangled symbols
		const std::uint8_t sizeof_Lib_Index{variable}; //number of bytes of Lib_Index type
		const Lib_Index lib_count{variable};		   //number of lib_db and lib_indexes entries

		File_Offset mangled_symbol_indexes[symbol_count + 1]; //offsets to sorted mangled symbols in mangled_symbol_db
		File_Offset lib_indexes[lib_count];					  //offsets of lib indexes, contains an extra entry for past symbol_db

		struct {
			char mangled_symbol[variable]; //null-terminated
			Lib_Index libs[variable];	   //index to lib_indexes that provide symbol. The end of libs is the index of the next symbol
		} mangled_symbol_db[symbol_count]; //indexed by both mangled_symbol_indexes and demangled_symbol_indexes
		char lib_db[variable][lib_count];  //unsorted list of library paths, null-terminated
	} // namespace For::documentation::of::file::format::only

} // namespace

#if __GNUC__
#pragma GCC diagnostic pop
#else
#pragma clang diagnostic pop
#endif

struct Extractor {
	template <std::unsigned_integral T>
	operator T() {
		return static_cast<T>(extract_number(sizeof(T)));
	}

	[[nodiscard]] std::uint64_t extract_number(std::size_t size) {
		assume(sizeof(std::uint64_t) >= size);
		assume(data.size() >= size);
		std::uint64_t retval{};
		for (std::size_t i = 0; i < size; i++) {
			retval |= (static_cast<std::uint8_t>(data.front()) + 0u) << (8 * i);
			data.remove_prefix(1);
		}
		return retval;
	}

	operator std::string_view() {
		std::string_view result{reinterpret_cast<const char *>(&data.front()), data.size()};
		const auto end_pos = result.find('\0');
		assume(end_pos != result.npos);
		result.remove_suffix(result.size() - end_pos);
		data.remove_prefix(end_pos + 1);
		return result;
	}

	operator std::filesystem::path() {
		return std::string_view{*this};
	}

	[[nodiscard]] Extractor operator[](std::size_t start) {
		assume(start <= data.size());
		data.remove_prefix(start);
		return {data};
	}

	[[nodiscard]] Extractor operator[](std::size_t start, std::size_t end) {
		assume(start <= end and end <= data.size());
		const auto result = data.substr(start, end - start);
		data.remove_prefix(end);
		return {result};
	}

	static void assume(bool condition) {
		if (not condition) {
			throw std::out_of_range{"Extractor failed extracting data, invalid input"};
		}
	}

	std::basic_string_view<std::byte> data;
};

template <>
[[nodiscard]] Extractor::operator bool() {
	return not data.empty();
}

template <class T>
static void leak(T &t) {
	alignas(T) char buffer[sizeof t];
	new (buffer) T(std::move(t));
}

static constexpr auto &magic_number = "LibFinderV1";
using File_Offset = std::uint32_t;

void Symbol_database::Writer::add(std::string mangled_symbol, std::size_t lib_id) {
	mangled_data.front().push_back({std::move(mangled_symbol), lib_id});
}

Symbol_database::Write_stats Symbol_database::Writer::write(std::filesystem::path path, const std::vector<std::string> &libraries) {
	Symbol_database::Write_stats stats;
	std::ofstream file{path, std::ios_base::out | std::ios_base::binary};
	std::map<std::string /*mangled_symbol*/, std::string /*libs*/> mangled_symbol_db;

	for (const auto &list : mangled_data) {
		for (auto &[symbol, lib] : list) {
			mangled_symbol_db[symbol].append(std::string_view{reinterpret_cast<const char *>(&lib), sizeof_Lib_Index});
		}
	}

	PROF << "to sort through symbols";

	stats.unique_symbols = mangled_symbol_db.size();

	file.write(magic_number, sizeof magic_number); //magic_number

	const auto &install_status = get_install_status();
	file.write(install_status.data(), Cast{install_status.size() + 1}); //install_status

	std::uint32_t symbol_count = Cast{mangled_symbol_db.size()};
	file.write(reinterpret_cast<const char *>(&symbol_count), sizeof(std::uint32_t)); //symbol_count

	file << sizeof_Lib_Index; //sizeof_Lib_Index

	const auto lib_count = libraries.size();
	file.write(reinterpret_cast<const char *>(&lib_count), sizeof_Lib_Index); //lib_count

	//seek past, fill out later
	const auto indexes_start = file.tellp();
	file.seekp(Cast{sizeof(File_Offset) * (symbol_count + 1) //mangled_symbol_indexes
					+ sizeof(File_Offset) * lib_count},		 //lib_indexes
			   std::ios_base::cur);

	//mangled_symbol_db
	stats.symbols_db_size = Cast{+file.tellp()};
	std::vector<File_Offset> mangled_symbol_indexes;
	mangled_symbol_indexes.reserve(symbol_count + 1);
	for (const auto &[symbol, libindexes] : mangled_symbol_db) {
		mangled_symbol_indexes.push_back(Cast{+file.tellp()});
		file.write(symbol.c_str(), Cast{symbol.size() + 1});
		file.write(libindexes.c_str(), Cast{libindexes.size()});
	}
	mangled_symbol_indexes.push_back(Cast{+file.tellp()});
	assert(mangled_symbol_indexes.size() == symbol_count + 1);
	stats.symbols_db_size = Cast<std::size_t>{+file.tellp()} - stats.symbols_db_size;

	//lib_db
	stats.libs_db_size = Cast{+file.tellp()};
	std::vector<File_Offset> lib_indexes;
	lib_indexes.reserve(libraries.size());
	for (auto &lib : libraries) {
		lib_indexes.push_back(Cast{+file.tellp()});
		file.write(lib.data(), Cast{lib.size() + 1});
	}
	stats.libs_db_size = Cast<std::size_t>{+file.tellp()} - stats.libs_db_size;

	//end of file, now rewind to fill indexes
	file.seekp(indexes_start, std::ios_base::beg);

	stats.symbols_index_size = Cast{+file.tellp()};

	//mangled_symbol_indexes
	file.write(reinterpret_cast<char *>(mangled_symbol_indexes.data()), Cast{mangled_symbol_indexes.size() * sizeof(File_Offset)});
	stats.symbols_index_size = Cast<std::size_t>{+file.tellp()} - stats.symbols_index_size;

	//lib_indexes
	stats.libs_index_size = Cast{+file.tellp()};
	file.write(reinterpret_cast<char *>(lib_indexes.data()), Cast{lib_indexes.size() * sizeof(File_Offset)});
	stats.libs_index_size = Cast<std::size_t>{+file.tellp()} - stats.libs_index_size;

	PROF << "to write results to disk";

	leak(mangled_symbol_indexes);
	leak(mangled_data);
	leak(mangled_symbol_db);

	return stats;
}

Symbol_database::Writer::Writer(std::size_t libraries)
	: sizeof_Lib_Index{1} {
	while (libraries > 255) {
		libraries /= 256;
		sizeof_Lib_Index++;
	}
}

void Symbol_database::Writer::merge(Writer &&other) {
	for (auto &d : other.mangled_data) {
		mangled_data.push_back(std::move(d));
	}
	other.mangled_data.push_back({});
}

std::size_t Symbol_database::Writer::symbol_count() const {
	std::size_t size{};
	for (auto &d : mangled_data) {
		size += d.size();
	}
	return size;
}

Symbol_database::Reader::Reader(std::filesystem::path path) {
	const int file = open(path.c_str(), O_RDONLY);
	const auto seek_result = lseek(file, 0, SEEK_END);
	if (seek_result == -1) {
		throw std::runtime_error{std::format("Failed seeking file {} because of errno {}", path, errno)};
	}
	const std::size_t data_size = Cast{seek_result};
	data = {static_cast<const std::byte *>(mmap(nullptr, data_size, PROT_READ, MAP_PRIVATE, file, 0)), data_size};
	close(file);

	Extractor ext{data};

	//magic_number
	if (std::string_view{ext[0, sizeof magic_number]} != magic_number) {
		throw std::runtime_error{std::format("{} is not a valid symbol database, magic number mismatch", path.c_str())};
	}

	//install_status
	std::string_view install_status{ext};
	if ((outdated = install_status != get_install_status())) {
		std::println(stderr, "{}: Outdated database, run {}", Color::warning("Warning"), Color::command("libfinder -u"));
	}

	//symbol_count
	symbol_count = ext;

	//sizeof_Lib_Index
	sizeof_Lib_Index = ext;

	//lib_count
	lib_count = ext.extract_number(sizeof_Lib_Index);

	//mangled_symbol_indexes
	mangled_symbol_indexes = ext[0, sizeof(File_Offset) * (symbol_count + 1)].data;

	//lib_indexes
	lib_indexes = ext.data;
}

Symbol_database::Reader::~Reader() {
	if (data.data()) {
		munmap(const_cast<void *>(static_cast<const void *>(data.data())), data.size());
	}
}

#define SYMBOL_DATABASE_MEMBERS X(data), X(symbol_count), X(sizeof_Lib_Index), X(lib_count), X(mangled_symbol_indexes), X(lib_indexes), X(outdated)

Symbol_database::Reader::Reader(Reader &&other)
	:
#define X(MEMBER)                                                                                                                                              \
	MEMBER {                                                                                                                                                   \
		std::move(other.MEMBER)                                                                                                                                \
	}
	SYMBOL_DATABASE_MEMBERS
#undef X
{
	other.data = {};
}

Symbol_database::Reader &Symbol_database::Reader::operator=(Symbol_database::Reader &&other) {
#define X(MEMBER) std::swap(MEMBER, other.MEMBER)
	(SYMBOL_DATABASE_MEMBERS);
#undef X
	return *this;
}

struct Symbol_database::Reader::Symbol_Db_Iterator {
	using value_type = std::string_view;
	using difference_type = std::ptrdiff_t;

	std::string_view operator*() const {
		return Extractor{reader->data}[offset()];
	}
	Symbol_Db_Iterator &operator++() {
		++index;
		return *this;
	}
	Symbol_Db_Iterator &operator--() {
		--index;
		return *this;
	}
	Symbol_Db_Iterator operator++(int) {
		return {.index = index++, .reader = reader};
	}
	Symbol_Db_Iterator operator--(int) {
		return {.index = index--, .reader = reader};
	}
	Symbol_Db_Iterator operator+(std::size_t offset) const {
		return {.index = index + offset, .reader = reader};
	}
	Symbol_Db_Iterator operator-(std::size_t offset) const {
		return {.index = index - offset, .reader = reader};
	}
	difference_type operator-(const Symbol_Db_Iterator &other) const {
		return static_cast<difference_type>(index) - static_cast<difference_type>(other.index);
	}
	Symbol_Db_Iterator &operator+=(std::size_t offset) {
		index += offset;
		return *this;
	}
	Symbol_Db_Iterator &operator-=(std::size_t offset) {
		index -= offset;
		return *this;
	}
	auto operator<=>(const Symbol_Db_Iterator &) const = default;
	File_Offset offset() const {
		return Extractor{reader->mangled_symbol_indexes}[index * sizeof(File_Offset)];
	}

	std::size_t index;
	const Symbol_database::Reader *reader;
};

Symbol_database::Reader::Symbol_Db_Iterator Symbol_database::Reader::begin() const {
	return {.index = 0, .reader = this};
}

Symbol_database::Reader::Symbol_Db_Iterator Symbol_database::Reader::end() const {
	return {.index = symbol_count, .reader = this};
}

std::vector<std::filesystem::path /*lib*/> Symbol_database::Reader::libraries_from_symbol(std::string symbol) const {
	auto it = std::lower_bound(begin(), end(), symbol);
	if (it == end() or *it != symbol) {
		return {};
	}
	return std::move(get_libraries(it, it + 1).begin()->second);
}

std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/>
Symbol_database::Reader::libraries_from_prefix(std::string symbol_prefixes) const {
	assert(([&] {
		auto first_non_sorted = std::is_sorted_until(begin(), end());
		if (first_non_sorted != end()) {
			std::cout << "Searching " << end().index - 1 << " symbols" << std::endl;
			std::cout << +(*first_non_sorted).front() << " at index " << first_non_sorted.index << std::endl;
			std::cout << " with libraries " << first_non_sorted.index << '\n';
			auto found_libs = get_libraries(first_non_sorted, first_non_sorted + 1);
			std::cout << found_libs.size() << std::flush;
			for (auto &lib : found_libs.begin()->second) {
				std::cout << '\t' << lib << std::endl;
			}
			return false;
		}
		return true;
	}()));
	const auto start_it = std::lower_bound(begin(), end(), symbol_prefixes);
	auto end_it = start_it;
	while (end_it != end() and (*end_it).starts_with(symbol_prefixes)) {
		++end_it;
	}
	return get_libraries(start_it, end_it);
}

bool Symbol_database::Reader::is_outdated() const {
	return outdated;
}

std::string_view Symbol_database::Reader::get_symbol(std::size_t index) const {
	File_Offset offset = Extractor{mangled_symbol_indexes}[index * sizeof(File_Offset)];
	return Extractor{data}[offset];
}

std::map<std::string_view, std::vector<std::filesystem::path>> Symbol_database::Reader::get_libraries(Symbol_Db_Iterator begin, Symbol_Db_Iterator end) const {
	std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/> result;
	while (begin < end) {
		auto start = begin.offset();
		begin++;
		auto ext = Extractor{data}[start, begin.offset()];
		auto &found_libs = result[ext];
		while (ext) {
			std::uint64_t lib_index = ext.extract_number(sizeof_Lib_Index);
			File_Offset lib_offset = Extractor{lib_indexes}[lib_index * sizeof(File_Offset)];
			found_libs.push_back(Extractor{data}[lib_offset]);
		}
	}
	return result;
}
