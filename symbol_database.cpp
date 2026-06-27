#include "symbol_database.h"
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

static auto make_sure(bool condition) {
	if (not condition) {
		throw std::runtime_error{"Invalid database format"};
	}
};

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

template <class Internal>
	requires(std::is_arithmetic_v<Internal>)
struct Cast {
	template <class From>
		requires(not std::is_same_v<From, Internal>)
	Cast(From v)
		: value{Cast{v}} {}
	template <class From>
		requires(std::is_same_v<From, Internal>)
	Cast(From v)
		: value{v} {}

	template <class To>
		requires(std::is_arithmetic_v<To>)
	operator To() && {
		if constexpr (std::is_signed_v<Internal> and std::is_unsigned_v<To>) {
			make_sure(value >= 0);
			make_sure(static_cast<std::make_unsigned_t<decltype(value)>>(value) <= std::numeric_limits<To>::max());
		} else if constexpr (std::is_unsigned_v<Internal> and std::is_signed_v<To>) {
			make_sure(value <= std::numeric_limits<To>::max());
		} else {
			make_sure(std::numeric_limits<To>::min() <= value and std::numeric_limits<To>::max() >= value);
		}
		return static_cast<To>(value);
	}

	Internal operator+() const {
		return value;
	}
	Internal value;
};

template <class T>
Cast(T) -> Cast<T>;

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
	stats.symbols_db_size = +Cast<std::size_t>{+file.tellp()} - stats.symbols_db_size;

	//lib_db
	stats.libs_db_size = Cast{+file.tellp()};
	std::vector<File_Offset> lib_indexes;
	lib_indexes.reserve(libraries.size());
	for (auto &lib : libraries) {
		lib_indexes.push_back(Cast{+file.tellp()});
		file.write(lib.data(), Cast{lib.size() + 1});
	}
	stats.libs_db_size = +Cast<std::size_t>{+file.tellp()} - stats.libs_db_size;

	//end of file, now rewind to fill indexes
	file.seekp(indexes_start, std::ios_base::beg);

	stats.symbols_index_size = Cast{+file.tellp()};

	//mangled_symbol_indexes
	file.write(reinterpret_cast<char *>(mangled_symbol_indexes.data()), Cast{mangled_symbol_indexes.size() * sizeof(File_Offset)});
	stats.symbols_index_size = +Cast<std::size_t>{+file.tellp()} - stats.symbols_index_size;

	//lib_indexes
	stats.libs_index_size = Cast{+file.tellp()};
	file.write(reinterpret_cast<char *>(lib_indexes.data()), Cast{lib_indexes.size() * sizeof(File_Offset)});
	stats.libs_index_size = +Cast<std::size_t>{+file.tellp()} - stats.libs_index_size;

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

//static auto to_sv(std::basic_string_view<uint8_t> usv, std::size_t offset = 0) {
//	usv.remove_prefix(offset);
//	return std::string_view{reinterpret_cast<const char *>(&usv.front()), reinterpret_cast<const char *>(&usv.back())};
//}

static auto to_nullterminated_sv(std::basic_string_view<uint8_t> usv, std::size_t offset = 0) {
	usv.remove_prefix(offset);
	auto end_pos = usv.find('\0');
	if (end_pos != usv.npos) {
		usv.remove_suffix(usv.size() - end_pos - 1);
	}
	return std::string_view{reinterpret_cast<const char *>(&usv.front()), reinterpret_cast<const char *>(&usv.back())};
}

struct String_View_Reader {
	std::basic_string_view<uint8_t> &cur;
	operator std::uint32_t() {
		std::uint32_t retval{};
		make_sure(cur.size() >= sizeof retval);
		for (std::size_t i = 0; i < sizeof retval; i++) {
			retval |= (cur[0] + 0u) << (8 * i);
			cur.remove_prefix(1);
		}
		return retval;
	}
	operator std::uint8_t() {
		std::uint8_t retval;
		make_sure(cur.size() >= sizeof retval);
		retval = cur[0];
		cur.remove_prefix(1);
		return retval;
	}
	std::size_t operator()(const std::size_t size) {
		std::uint32_t retval{};
		make_sure(cur.size() >= size);
		for (std::size_t i = 0; i < size; i++) {
			retval |= (cur[0] + 0u) << (8 * i);
			cur.remove_prefix(1);
		}
		return retval;
	}
};

Symbol_database::Reader::Reader(std::filesystem::path path) {
	int file = open(path.c_str(), O_RDONLY);
	const auto data_size = std::filesystem::file_size(path);
	data = {static_cast<const std::uint8_t *>(mmap(nullptr, data_size, PROT_READ, MAP_PRIVATE, file, 0)), data_size};
	close(file);
	auto cur = data;

	auto cur_sv = [&cur] { return to_nullterminated_sv(cur); };

	String_View_Reader cur_to{cur};

	//magic_number
	if (cur_sv() != magic_number) {
		throw std::runtime_error{std::format("{} is not a valid symbol database, magic number mismatch", path.c_str())};
	}
	cur.remove_prefix(sizeof(magic_number));

	//install_status
	auto install_status = to_nullterminated_sv(cur);
	if ((outdated = install_status != get_install_status())) {
		std::println(stderr, "{}: Outdated database, run {}", Color::warning("Warning"), Color::command("libfinder -u"));
	}
	cur.remove_prefix(install_status.size() + 1);

	//symbol_count
	symbol_count = cur_to;

	//sizeof_Lib_Index
	sizeof_Lib_Index = cur_to;

	//lib_count
	lib_count = cur_to(sizeof_Lib_Index);

	//mangled_symbol_indexes
	mangled_symbol_indexes = cur;
	mangled_symbol_indexes.remove_suffix(mangled_symbol_indexes.size() - sizeof(File_Offset) * (symbol_count + 1));
	cur.remove_prefix(mangled_symbol_indexes.size());

	//lib_indexes
	lib_indexes = cur.data();
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
		return to_nullterminated_sv(reader->data, offset());
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
		File_Offset result;
		auto offset_data = reader->mangled_symbol_indexes;
		offset_data.remove_prefix(index * sizeof(File_Offset));
		String_View_Reader svr{offset_data};
		result = svr;
		return result;
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
	File_Offset offset;
	auto offset_data = mangled_symbol_indexes;
	offset_data.remove_prefix(index * sizeof(File_Offset));
	String_View_Reader svr{offset_data};
	offset = svr;
	auto symbol_data = data;
	symbol_data.remove_prefix(offset);
	return to_nullterminated_sv(symbol_data);
}

std::map<std::string_view, std::vector<std::filesystem::path>> Symbol_database::Reader::get_libraries(Symbol_Db_Iterator begin, Symbol_Db_Iterator end) const {
	std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/> result;
	while (begin < end) {
		auto &found_libs = result[*begin];
		const std::uint8_t *cur = data.data() + begin.offset();
		while (*cur++)
			;
		++begin;
		const auto end_pos = data.data() + begin.offset();
		while (cur < end_pos) {
			std::size_t lib_index{};
			std::memcpy(&lib_index, cur, sizeof_Lib_Index);
			File_Offset lib_offset;
			std::memcpy(&lib_offset, lib_indexes + lib_index * sizeof(File_Offset), sizeof(File_Offset));
			found_libs.push_back(to_nullterminated_sv(data.data() + lib_offset));
			cur += sizeof_Lib_Index;
		}
	}
	return result;
}
