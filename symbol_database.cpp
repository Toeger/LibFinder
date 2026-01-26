#include "symbol_database.h"

#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/mman.h>
#include <unistd.h>

/* Symbol database file format
 *
 * Type <Symbol_index> is an index type with variable byte size symbol_index_size
 * Type <Lib_index> is an index type with variable byte size lib_index_size
 * Type <Offset> is a file offset with fixed type <std::uint32_t>
 *
 * <std::uint8_t> symbol_index_size; //number of bytes of Symbol_index type
 * <std::uint8_t> lib_index_size; //number of bytes of Lib_index type
 * <Symbol_index> symbols; //number of symbol entries
 * <Lib_index> libs; //number of lib entries
 * <Offset> symbol_indexes_start;
 * <Offset> lib_indexes_start;
 * <char *symbol\0><Lib_index libs[]> symbol_db[db_index_size]; //symbol entries of variable size, indexed by symbol_indexes
 * <Offset> symbol_indexes[symbols]; //offsets of symbols indexes
 * <char *lib\0> lib_db[lib_index_size]; //lib entries of variable size, indexed by lib_indexes
 * <Offset> lib_indexes[libs]; //offsets of lib indexes
 */

using Offset = std::uint32_t;

void Symbol_database::Writer::add(std::string symbol, std::string library) {
	data.front().push_back({std::move(symbol), std::move(library)});
}

void Symbol_database::Writer::write(std::filesystem::path path) const {
	std::ofstream file{path, std::ios_base::out | std::ios_base::binary};
	std::map<std::string /*libpath*/, std::size_t /*libindex*/> lib_db;
	std::map<std::string /*symbol*/, std::vector<std::size_t /*libindex*/>> symbol_db;
	for (auto &list : data) {
		for (auto &entry : list) {
			const auto lib_index = lib_db.insert({std::move(entry.second), lib_db.size()}).first->second;
			symbol_db[std::move(entry.first)].push_back(lib_index);
		}
	}

	std::uint8_t symbol_index_size{};
	for (auto size = symbol_db.size(); size; size >>= 8) {
		symbol_index_size++;
	}
	file << symbol_index_size;

	std::uint8_t lib_index_size{};
	for (auto size = symbol_db.size(); size; size >>= 8) {
		lib_index_size++;
	}
	file << lib_index_size;

	auto symbols = symbol_db.size();
	file.write(reinterpret_cast<const char *>(&symbols), symbol_index_size);

	auto libs = lib_db.size();
	file.write(reinterpret_cast<const char *>(&libs), lib_index_size);

	Offset symbol_indexes_start{};
	const Offset symbol_index_start_pos = file.tellp();
	file.write(reinterpret_cast<const char *>(&symbol_indexes_start), sizeof(Offset)); //placeholder

	Offset lib_indexes_start{};
	file.write(reinterpret_cast<const char *>(&lib_indexes_start), sizeof(Offset)); //placeholder

	//symbol_db
	std::vector<Offset> symbol_indexes;
	symbol_indexes.reserve(symbols);
	for (auto &[symbol, libindexes] : symbol_db) {
		symbol_indexes.push_back(file.tellp());
		file << symbol << '\0';
		for (auto &index : libindexes) {
			file.write(reinterpret_cast<const char *>(&index), lib_index_size);
		}
	}

	//symbol_indexes
	symbol_indexes_start = file.tellp();
	symbol_indexes.push_back(symbol_indexes_start);
	file.write(reinterpret_cast<char *>(symbol_indexes.data()), symbol_indexes.size() * sizeof(Offset));

	//lib_db
	std::vector<Offset> lib_indexes;
	lib_indexes.resize(libs);
	for (auto &[lib, lib_index] : lib_db) {
		lib_indexes[lib_index] = file.tellp();
		file << lib << '\0';
	}

	//lib_indexes
	lib_indexes_start = file.tellp();
	file.write(reinterpret_cast<char *>(lib_indexes.data()), lib_indexes.size() * sizeof(Offset));

	//fill placeholders
	file.seekp(symbol_index_start_pos);
	file.write(reinterpret_cast<const char *>(&symbol_indexes_start), sizeof(Offset));
	file.write(reinterpret_cast<const char *>(&lib_indexes_start), sizeof(Offset));
}

void Symbol_database::Writer::merge(Writer &&other) {
	for (auto &d : other.data) {
		data.push_back(std::move(d));
	}
	other.data.push_back({});
}

Symbol_database::Reader::Reader(std::filesystem::path path)
	: data_size{std::filesystem::file_size(path)} {
	int file = open(path.c_str(), O_RDONLY);
	data = static_cast<const uint8_t *>(mmap(nullptr, data_size, PROT_READ, MAP_PRIVATE, file, 0));
	close(file);
	const std::uint8_t *cur = data;
	symbol_index_size = *cur++;
	lib_index_size = *cur++;
	std::memcpy(&symbols, cur, symbol_index_size);
	cur += symbol_index_size;
	std::memcpy(&libs, cur, lib_index_size);
	cur += symbol_index_size;
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
}

Symbol_database::Reader::~Reader() {
	if (data) {
		munmap(const_cast<void *>(static_cast<const void *>(data)), data_size);
	}
}

Symbol_database::Reader::Reader(Reader &&other)
	: data_size{other.data_size} {
	data = other.data;
	other.data = nullptr;
}

Symbol_database::Reader &Symbol_database::Reader::operator=(Symbol_database::Reader &&other) {
	std::swap(data_size, other.data_size);
	std::swap(data, other.data);
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
	std::size_t offset() const {
		std::size_t result{};
		std::memcpy(&result, reader->symbol_indexes + index * sizeof(Offset), reader->symbol_index_size);
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

std::vector<std::string_view> Symbol_database::Reader::get_libraries(std::string_view symbol, bool prefix_search) const {
	if (prefix_search) {
		auto range = std::equal_range(begin(), end(), symbol, [](std::string_view lhs, std::string_view rhs) { return lhs < rhs; });
		return get_libraries(range.first, range.second);
	}
	auto it = std::lower_bound(begin(), end(), symbol);
	if (*it != symbol) {
		return {};
	}
	return get_libraries(it, it + 1);
}

std::string_view Symbol_database::Reader::get_symbol(std::size_t index) {
	Offset offset;
	std::memcpy(&offset, symbol_indexes + index * sizeof(Offset), sizeof(Offset));
	return {reinterpret_cast<const char *>(data + offset), sizeof(Offset)};
}

std::vector<std::string_view> Symbol_database::Reader::get_libraries(Symbol_db_iterator begin, Symbol_db_iterator end) const {
	std::vector<std::string_view> result;
	while (begin < end) {
		const std::uint8_t *cur = data + begin.offset();
		while (*cur++)
			;
		++begin;
		const auto end_pos = data + begin.offset();
		while (cur < end_pos) {
			std::size_t lib_index{};
			std::memcpy(&lib_index, cur, lib_index_size);
			cur += lib_index_size;
			const auto lib_indexes_offset = lib_indexes - data;
			std::cout << lib_indexes_offset;
			Offset lib_offset;
			std::memcpy(&lib_offset, lib_indexes + lib_index * sizeof(Offset), sizeof(Offset));
			result.push_back(reinterpret_cast<const char *>(data + lib_offset));
		}
	}
	return result;
}
