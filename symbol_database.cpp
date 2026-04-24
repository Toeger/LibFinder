#include "symbol_database.h"
#include "color.h"
#include "profile.h"
#include "utility.h"

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

/* Symbol database file format
 * Type Lib_index is an index type with variable byte size lib_index_size
 * Type Offset is a file offset, alias for std::uint32_t
 *
 * char magic_number[12] = "LibFinderV1";
 * char install_status[]; //the state of the packages when the database was created, can be empty for unkown
 * std::uint8_t lib_index_size; //number of bytes of Lib_index type
 * std::uint32_t symbols; //number of symbol entries
 * Lib_index libs; //number of lib entries
 * Offset symbol_indexes_start;
 * Offset lib_indexes_start;
 * [char *symbol, '\0', Lib_index libs[]] symbol_db[db_index_size]; //symbol entries of variable size, indexed by symbol_indexes
 * Offset symbol_indexes[symbols]; //offsets of symbols indexes
 * char lib_db[][lib_index_size]; //lib entries of variable size, indexed by lib_indexes
 * Offset lib_indexes[libs]; //offsets of lib indexes, contains an extra entry for past symbol_db
 */

template <class T>
static void leak(T &t) {
	alignas(T) char buffer[sizeof t];
	new (buffer) T(std::move(t));
}

static constexpr auto &magic_number = "LibFinderV1";
using Offset = std::uint32_t;

void Symbol_database::Writer::add(std::string symbol, std::size_t lib_id) {
	data.front().push_back({std::move(symbol), lib_id});
}

Symbol_database::Write_stats Symbol_database::Writer::write(std::filesystem::path path, const std::vector<std::string> &libraries) {
	Symbol_database::Write_stats stats;
	std::ofstream file{path, std::ios_base::out | std::ios_base::binary};
	std::map<std::string /*symbol*/, std::string /*libs*/> symbol_db;
	for (auto &list : data) {
		for (auto &[symbol, lib] : list) {
			symbol_db[symbol].append(std::string_view{reinterpret_cast<const char *>(&lib), lib_index_size});
		}
	}

	PROF << "to sort through symbols";

	stats.unique_symbols = symbol_db.size();

	file.write(magic_number, sizeof magic_number);

	const auto &install_status = get_install_status();
	file.write(install_status.data(), install_status.size() + 1);

	file << lib_index_size;

	std::uint32_t symbols = symbol_db.size();
	file.write(reinterpret_cast<const char *>(&symbols), sizeof(std::uint32_t));

	auto libs = libraries.size();
	file.write(reinterpret_cast<const char *>(&libs), lib_index_size);

	Offset symbol_indexes_start{};
	const Offset symbol_index_start_pos = +file.tellp();
	file.write(reinterpret_cast<const char *>(&symbol_indexes_start), sizeof(Offset)); //placeholder

	Offset lib_indexes_start{};
	file.write(reinterpret_cast<const char *>(&lib_indexes_start), sizeof(Offset)); //placeholder

	//symbol_db
	std::vector<Offset> symbol_indexes;
	stats.symbols_db_size = +file.tellp();
	symbol_indexes.reserve(symbols);
	for (const auto &[symbol, libindexes] : symbol_db) {
		symbol_indexes.push_back(+file.tellp());
		file.write(symbol.c_str(), symbol.size() + 1);
		file.write(libindexes.c_str(), libindexes.size());
	}
	stats.symbols_db_size = +file.tellp() - stats.symbols_db_size;

	//symbol_indexes
	stats.symbols_index_size = +file.tellp();
	symbol_indexes_start = +file.tellp();
	symbol_indexes.push_back(symbol_indexes_start);
	file.write(reinterpret_cast<char *>(symbol_indexes.data()), symbol_indexes.size() * sizeof(Offset));
	stats.symbols_index_size = +file.tellp() - stats.symbols_index_size;

	//lib_db
	std::vector<Offset> lib_indexes;
	stats.libs_db_size = +file.tellp();
	lib_indexes.reserve(libraries.size());
	for (auto &lib : libraries) {
		lib_indexes.push_back(+file.tellp());
		file.write(lib.data(), lib.size() + 1);
	}
	stats.libs_db_size = +file.tellp() - stats.libs_db_size;

	//lib_indexes
	stats.libs_index_size = +file.tellp();
	lib_indexes_start = +file.tellp();
	file.write(reinterpret_cast<char *>(lib_indexes.data()), lib_indexes.size() * sizeof(Offset));
	stats.libs_index_size = +file.tellp() - stats.libs_index_size;

	//fill placeholders
	file.seekp(symbol_index_start_pos);
	file.write(reinterpret_cast<const char *>(&symbol_indexes_start), sizeof(Offset));
	file.write(reinterpret_cast<const char *>(&lib_indexes_start), sizeof(Offset));

	PROF << "to write results to disk";

	leak(data);
	leak(symbol_db);

	return stats;
}

Symbol_database::Writer::Writer(std::size_t libraries)
	: lib_index_size{1} {
	while (libraries > 255) {
		libraries /= 256;
		lib_index_size++;
	}
}

void Symbol_database::Writer::merge(Writer &&other) {
	for (auto &d : other.data) {
		data.push_back(std::move(d));
	}
	other.data.push_back({});
}

std::size_t Symbol_database::Writer::size() const {
	std::size_t size{};
	for (auto &d : data) {
		size += d.size();
	}
	return size;
}

Symbol_database::Reader::Reader(std::filesystem::path path)
	: data_size{std::filesystem::file_size(path)} {
	int file = open(path.c_str(), O_RDONLY);
	data = static_cast<const uint8_t *>(mmap(nullptr, data_size, PROT_READ, MAP_PRIVATE, file, 0));
	close(file);
	const std::uint8_t *cur = data;
	if (std::memcmp(data, magic_number, sizeof magic_number)) {
		throw std::runtime_error{std::format("{} is not a valid symbol database, magic number mismatch", path.c_str())};
	}
	cur += sizeof(magic_number);
	const auto end_install_status = std::find(cur, data + data_size, '\0');
	std::string_view install_status{reinterpret_cast<const char *>(cur), reinterpret_cast<const char *>(end_install_status)};
	if ((outdated = install_status != get_install_status())) {
		std::println(stderr, "{}: Outdated database, run {}", Color::warning("Warning"), Color::command("libfinder -u"));
	}
	cur = end_install_status + 1;
	lib_index_size = *cur++;
	std::memcpy(&symbols, cur, sizeof(std::uint32_t));
	cur += sizeof(std::uint32_t);
	std::memcpy(&libs, cur, lib_index_size);
	cur += lib_index_size;
	Offset symbol_indexes_start;
	std::memcpy(&symbol_indexes_start, cur, sizeof(Offset));
	cur += sizeof(Offset);
	symbol_indexes = data + symbol_indexes_start;
	Offset lib_indexes_start;
	std::memcpy(&lib_indexes_start, cur, sizeof(Offset));
	cur += sizeof(Offset);
	lib_indexes = data + lib_indexes_start;
	symbol_db = cur;
	lib_db = data + symbol_indexes_start + (symbols + 1) * sizeof(Offset);
	assert((
		[lib_indexes_start, libs = libs, data = data] {
			for (std::size_t lib = 0; lib < libs; lib++) {
				Offset pos;
				std::memcpy(&pos, data + lib_indexes_start + lib * sizeof(Offset), sizeof(Offset));
				assert(data[pos] == '/');
				//std::cout << lib << ": " << data + pos << std::endl;
			}
		}(),
		true));
	assert((
		[symbol_indexes_start, symbols = symbols, data = data] {
			for (std::size_t symbol = 0; symbol < symbols; symbol++) {
				Offset pos;
				std::memcpy(&pos, data + symbol_indexes_start + symbol * sizeof(Offset), sizeof(Offset));
				//std::cout << symbol << ": " << data + pos << std::endl;
			}
		}(),
		true));
}

Symbol_database::Reader::~Reader() {
	if (data) {
		munmap(const_cast<void *>(static_cast<const void *>(data)), data_size);
	}
}

#define SYMBOL_DATABASE_MEMBERS                                                                                                                                \
	X(data), X(data_size), X(symbols), X(libs), X(symbol_db), X(symbol_indexes), X(lib_db), X(lib_indexes), X(lib_index_size), X(outdated)

Symbol_database::Reader::Reader(Reader &&other)
	:
#define X(MEMBER)                                                                                                                                              \
	MEMBER {                                                                                                                                                   \
		std::move(other.MEMBER)                                                                                                                                \
	}
	SYMBOL_DATABASE_MEMBERS
#undef X
{
	other.data = nullptr;
}

Symbol_database::Reader &Symbol_database::Reader::operator=(Symbol_database::Reader &&other) {
#define X(MEMBER) std::swap(MEMBER, other.MEMBER)
	(SYMBOL_DATABASE_MEMBERS);
#undef X
	return *this;
}

struct Symbol_database::Reader::Symbol_db_iterator {
	using value_type = std::string_view;
	using difference_type = std::ptrdiff_t;

	std::string_view operator*() const {
		return reinterpret_cast<const char *>(reader->data + offset());
	}
	Symbol_db_iterator &operator++() {
		++index;
		return *this;
	}
	Symbol_db_iterator &operator--() {
		--index;
		return *this;
	}
	Symbol_db_iterator operator++(int) {
		return {.index = index++, .reader = reader};
	}
	Symbol_db_iterator operator--(int) {
		return {.index = index--, .reader = reader};
	}
	Symbol_db_iterator operator+(std::size_t offset) const {
		return {.index = index + offset, .reader = reader};
	}
	Symbol_db_iterator operator-(std::size_t offset) const {
		return {.index = index - offset, .reader = reader};
	}
	difference_type operator-(const Symbol_db_iterator &other) const {
		return static_cast<difference_type>(index) - static_cast<difference_type>(other.index);
	}
	Symbol_db_iterator &operator+=(std::size_t offset) {
		index += offset;
		return *this;
	}
	Symbol_db_iterator &operator-=(std::size_t offset) {
		index -= offset;
		return *this;
	}
	auto operator<=>(const Symbol_db_iterator &) const = default;
	Offset offset() const {
		Offset result;
		std::memcpy(&result, reader->symbol_indexes + index * sizeof(Offset), sizeof(Offset));
		return result;
	}

	std::size_t index;
	const Symbol_database::Reader *reader;
};

Symbol_database::Reader::Symbol_db_iterator Symbol_database::Reader::begin() const {
	return {.index = 0, .reader = this};
}

Symbol_database::Reader::Symbol_db_iterator Symbol_database::Reader::end() const {
	return {.index = symbols, .reader = this};
}

std::vector<std::filesystem::path /*lib*/> Symbol_database::Reader::libraries_from_symbol(std::string_view symbol) const {
	auto it = std::lower_bound(begin(), end(), symbol);
	if (it == end() or *it != symbol) {
		return {};
	}
	return std::move(get_libraries(it, it + 1).begin()->second);
}

std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/>
Symbol_database::Reader::libraries_from_prefix(std::string_view prefix) const {
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
	const auto start_it = std::lower_bound(begin(), end(), prefix);
	auto end_it = start_it;
	while (end_it != end() and (*end_it).starts_with(prefix)) {
		++end_it;
	}
	return get_libraries(start_it, end_it);
}

bool Symbol_database::Reader::is_outdated() const {
	return outdated;
}

std::string_view Symbol_database::Reader::get_symbol(std::size_t index) {
	Offset offset;
	std::memcpy(&offset, symbol_indexes + index * sizeof(Offset), sizeof(Offset));
	return {reinterpret_cast<const char *>(data + offset), sizeof(Offset)};
}

std::map<std::string_view, std::vector<std::filesystem::path>> Symbol_database::Reader::get_libraries(Symbol_db_iterator begin, Symbol_db_iterator end) const {
	std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/> result;
	while (begin < end) {
		auto &found_libs = result[*begin];
		const std::uint8_t *cur = data + begin.offset();
		while (*cur++)
			;
		++begin;
		const auto end_pos = data + begin.offset();
		while (cur < end_pos) {
			std::size_t lib_index{};
			std::memcpy(&lib_index, cur, lib_index_size);
			Offset lib_offset;
			std::memcpy(&lib_offset, lib_indexes + lib_index * sizeof(Offset), sizeof(Offset));
			found_libs.push_back(reinterpret_cast<const char *>(data + lib_offset));
			cur += lib_index_size;
		}
	}
	return result;
}
