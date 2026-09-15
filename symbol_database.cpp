#include "symbol_database.h"
#include "cast.h"
#include "color.h"
#include "profile.h"
#include "system.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <print>
#include <set>
#include <string>
#include <utility>

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#error "TODO: Implement Windows version"
#endif

#if __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#else
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace {
	namespace { //exposition only, not part of the file

		constexpr auto variable = not 0;   //indicates variable value
		using Lib_Index_Type = unsigned;   //an index type with variable byte size sizeof_Lib_Index_Type
		using File_Offset = std::uint32_t; //4 byte file offset type, offset from start of database file
	} // namespace

	namespace For::documentation::of::file::format::only {
		char magic_number[12] = "LibFinderV1"; //if it doesn't match file content, this documentation does not apply, exactly 12 bytes, null-terminated

		char install_status[variable] = {variable}; //the "state" of the system when the database was created, used to determine update needs, null-terminated

		const std::uint32_t symbol_count = variable; //number of demangled symbols

		std::uint8_t sizeof_Lib_Index = variable; //number of bytes of Lib_Index_Type, not necessarily a power of 2

		File_Offset mangled_symbol_indexes[symbol_count + 1]; //offsets to sorted mangled symbols in mangled_symbol_db, extra entry for end of mangled_symbol_db

		File_Offset lib_indexes[variable] = {variable}; //offsets of lib indexes

		struct {
			char mangled_symbol[variable]; //null-terminated
			Lib_Index_Type libs[variable]; //index to lib_indexes that provide symbol. The end of libs is the offset of the next symbol
		} mangled_symbol_db[symbol_count]; //indexed by mangled_symbol_indexes

		char lib_db[variable]; //unsorted list of library paths, null-terminated
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

void Symbol_database::Writer::add(std::string mangled_symbol, std::size_t lib_id) {
	mangled_data.front().push_back({std::move(mangled_symbol), lib_id});
}

struct File_Writer {
	using Pos_Type = long; //should be std::ostream::pos_type, but that one causes conversion issues

	File_Writer &operator<<(std::string_view sv) {
		os.write(sv.data(), Cast{sv.size()});
		return *this;
	}

	File_Writer &operator<<(char c) {
		os << c;
		return *this;
	}

	File_Writer &operator<<(std::unsigned_integral auto n)
		requires(not std::is_same_v<decltype(n), bool>)
	{
		write_dynamic(n, sizeof(n));
		return *this;
	}

	template <class T>
	File_Writer &operator<<(std::vector<T> v) {
		os.write(reinterpret_cast<const char *>(v.data()), Cast{std::size(v)} * sizeof(v.front()));
		return *this;
	}

	File_Writer &operator<<(const std::filesystem::path &path) {
		return *this << std::string_view{path.native()};
	}

	File_Writer &operator<<(const std::string &s) {
		return *this << std::string_view{s};
	}

	template <std::size_t size>
	File_Writer &operator<<(const char (&s)[size]) {
		return *this << std::string_view{s, size};
	}

	void write_dynamic(std::size_t value, std::size_t size) {
		os.write(reinterpret_cast<const char *>(&value), Cast{size});
	}

	Cast<Pos_Type> pos() const {
		return +os.tellp();
	}

	void operator+=(Pos_Type pos) {
		os.seekp(pos, std::ios_base::cur);
	}

	void operator=(Pos_Type pos) {
		os.seekp(pos, std::ios_base::beg);
	}

	operator Pos_Type() const {
		return os.tellp();
	}

	std::ostream &os;
};

static std::uint8_t bytes_required_for(std::size_t n) {
	std::uint8_t retval{};
	for (; n; n /= 256) {
		retval++;
	}
	return retval;
}

Symbol_database::Write_stats Symbol_database::Writer::write(std::filesystem::path output_file, const std::vector<std::filesystem::path> &libraries) {
	Symbol_database::Write_stats stats;
	std::ofstream file{output_file, std::ios_base::out | std::ios_base::binary};
	File_Writer writer{file};
	const std::uint8_t sizeof_Lib_Index = bytes_required_for(libraries.size());

	std::map<std::string /*mangled_symbol*/, std::string /*libs*/> mangled_symbol_db;

	for (const auto &list : mangled_data) {
		for (auto &[symbol, lib] : list) {
			mangled_symbol_db[symbol].append(std::string_view{reinterpret_cast<const char *>(&lib), sizeof_Lib_Index});
		}
	}

	PROF << "to sort through symbols";

	stats.unique_symbols = mangled_symbol_db.size();

	writer << magic_number;

	writer << get_install_status() << '\0';

	std::uint32_t symbol_count = Cast{mangled_symbol_db.size()};
	writer << symbol_count;

	writer << sizeof_Lib_Index;

	//seek past mangled_symbol_indexes, fill out later
	const long mangled_symbol_indexes_start = writer.pos();
	writer += Cast{sizeof(File_Offset)} * (symbol_count + 1) //mangled_symbol_indexes
			  + sizeof(File_Offset) * libraries.size();		 //lib_indexes

	//mangled_symbol_db
	const std::size_t mangled_symbol_db_start_pos = writer.pos();
	std::vector<File_Offset> mangled_symbol_indexes;
	mangled_symbol_indexes.reserve(symbol_count + 1);
	for (const auto &[symbol, libindexes] : mangled_symbol_db) {
		mangled_symbol_indexes.push_back(writer.pos());
		writer << symbol << '\0' << libindexes;
	}
	mangled_symbol_indexes.push_back(writer.pos());
	assert(mangled_symbol_indexes.size() == symbol_count + 1);
	const std::size_t mangled_symbol_db_end_pos = writer.pos();
	stats.symbols_db_size = mangled_symbol_db_end_pos - mangled_symbol_db_start_pos;

	//lib_db
	const std::size_t libs_db_start_pos = writer.pos();
	std::vector<File_Offset> lib_indexes;
	lib_indexes.reserve(libraries.size());
	for (auto &lib : libraries) {
		lib_indexes.push_back(writer.pos());
		writer << lib << '\0';
	}
	const std::size_t libs_db_end_pos = writer.pos();
	stats.libs_db_size = libs_db_end_pos - libs_db_start_pos;

	//end of file, now rewind to fill indexes
	writer = mangled_symbol_indexes_start;

	//mangled_symbol_indexes
	writer << mangled_symbol_indexes;
	stats.symbols_index_size = writer.pos() - mangled_symbol_indexes_start;

	//lib_indexes
	const std::size_t lib_indexes_start_pos = writer.pos();
	writer << lib_indexes;
	stats.libs_index_size = writer.pos() - lib_indexes_start_pos;

	PROF << "to write results to disk";

	leak(mangled_symbol_indexes);
	leak(mangled_data);
	leak(mangled_symbol_db);

	return stats;
}

std::pair<Symbol_database::Writer, std::vector<std::filesystem::path>>
Symbol_database::Writer::from_db_file(std::filesystem::path db_file, std::function_ref<bool(std::string_view symbol, std::filesystem::path library)> filter) {
	Reader reader{db_file};
	Writer writer;
	std::vector<std::filesystem::path> libraries{""};
	reader.libraries_from_prefix("", [&libraries, &writer, filter](std::string_view symbol, std::filesystem::path lib) {
		if (not filter(symbol, lib)) {
			return;
		}
		if (lib != libraries.back()) {
			libraries.push_back(lib);
		}
		writer.add(std::string{symbol}, libraries.size() - 1);
	});
	return {writer, libraries};
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
	{
		const int file = open(path.c_str(), O_RDONLY);
		if (file == -1) {
			throw std::runtime_error{std::format("Errno {}: Failed opening file {}", errno, path)};
		}
		struct Close {
			~Close() {
				close(file);
			}
			int file;
		} const _{file};
		const auto seek_result = lseek(file, 0, SEEK_END);
		if (seek_result == -1) {
			throw std::runtime_error{std::format("Errno {}: Failed seeking file {}", errno, path)};
		}
		const std::size_t data_size = Cast{seek_result};
		data = {static_cast<const std::byte *>(mmap(nullptr, data_size, PROT_READ, MAP_PRIVATE, file, 0)), data_size};
		if (data.data() == MAP_FAILED) {
			throw std::runtime_error{std::format("Errno {}: Failed memory-mapping {}", errno, path)};
		}
	}

	Extractor ext{data};

	if (std::string_view{ext[0, sizeof magic_number]} != magic_number) {
		throw std::runtime_error{std::format("{} is not a valid symbol database, magic number mismatch", path.c_str())};
	}

	std::string_view install_status{ext};
	if ((outdated = install_status != get_install_status())) {
		std::println(stderr, "{}: Outdated database, run {}", Color::warning("Warning"), Color::command("libfinder -u"));
	}

	symbol_count = ext;

	sizeof_Lib_Index = ext;

	mangled_symbol_indexes = ext[0, sizeof(File_Offset) * (symbol_count + 1)].data;

	lib_indexes = ext.data;
}

Symbol_database::Reader::~Reader() {
	if (data.data()) {
		munmap(const_cast<void *>(static_cast<const void *>(data.data())), data.size());
	}
}

#define SYMBOL_DATABASE_MEMBERS X(data), X(symbol_count), X(sizeof_Lib_Index), X(mangled_symbol_indexes), X(lib_indexes), X(outdated)

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

void Symbol_database::Reader::get_libraries(Symbol_Db_Iterator begin, Symbol_Db_Iterator end,
											std::invocable<std::string_view, std::filesystem::path> auto &&callback) const {
	while (begin < end) {
		auto start = begin.offset();
		begin++;
		auto ext = Extractor{data}[start, begin.offset()];
		const std::string_view symbol = ext;
		while (ext) {
			std::uint64_t lib_index = ext.extract_number(sizeof_Lib_Index);
			File_Offset lib_offset = Extractor{lib_indexes}[lib_index * sizeof(File_Offset)];
			callback(symbol, Extractor{data}[lib_offset]);
		}
	}
}

Symbol_database::Reader::Symbol_Db_Iterator Symbol_database::Reader::begin() const {
	return {.index = 0, .reader = this};
}

Symbol_database::Reader::Symbol_Db_Iterator Symbol_database::Reader::end() const {
	return {.index = symbol_count, .reader = this};
}

void Symbol_database::Reader::libraries_from_symbol(std::string symbol, std::function_ref<void(std::filesystem::path)> callback) const {
	auto it = std::lower_bound(begin(), end(), symbol);
	if (it == end() or *it != symbol) {
		return;
	}
	get_libraries(it, it + 1, [&callback](std::string_view, std::filesystem::path path) { callback(path); });
}

void Symbol_database::Reader::libraries_from_prefix(std::string symbol_prefixes,
													std::function_ref<void(std::string_view symbol, std::filesystem::path lib)> callback) const {
	assert(([&] {
		auto first_non_sorted = std::is_sorted_until(begin(), end());
		if (first_non_sorted != end()) {
			std::cout << "Searching " << end().index - 1 << " symbols" << std::endl;
			std::cout << +(*first_non_sorted).front() << " at index " << first_non_sorted.index << std::endl;
			std::cout << " with libraries " << first_non_sorted.index << '\n';
			get_libraries(first_non_sorted, first_non_sorted + 1, [](std::string_view, std::filesystem::path lib) { std::cout << '\t' << lib << std::endl; });
			return false;
		}
		return true;
	}()));
	const auto start_it = std::lower_bound(begin(), end(), symbol_prefixes);
	auto end_it = start_it;
	while (end_it != end() and (*end_it).starts_with(symbol_prefixes)) {
		++end_it;
	}
	return get_libraries(start_it, end_it, callback);
}

bool Symbol_database::Reader::is_outdated() const {
	return outdated;
}

std::string_view Symbol_database::Reader::get_symbol(std::size_t index) const {
	File_Offset offset = Extractor{mangled_symbol_indexes}[index * sizeof(File_Offset)];
	return Extractor{data}[offset];
}
