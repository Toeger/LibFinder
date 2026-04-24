#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Symbol_database {
	struct Write_stats {
		std::size_t unique_symbols;
		std::size_t symbols_db_size;
		std::size_t libs_db_size;
		std::size_t symbols_index_size;
		std::size_t libs_index_size;
	};

	struct Writer {
		Writer(std::size_t libraries);
		void add(std::string symbol, std::size_t lib_id);
		Write_stats write(std::filesystem::path path, const std::vector<std::string> &libraries);
		void merge(Writer &&other);
		std::size_t size() const;

		private:
		std::vector<std::vector<std::pair<std::string /*symbol*/, std::size_t /*libindex*/>>> data{{}};
		std::uint8_t lib_index_size;
	};

	struct Reader {
		Reader(std::filesystem::path path);
		~Reader();
		Reader(Reader &&other);
		Reader &operator=(Reader &&other);
		std::vector<std::filesystem::path> libraries_from_symbol(std::string_view symbol) const;
		std::map<std::string_view, std::vector<std::filesystem::path>> libraries_from_prefix(std::string_view symbol_prefixes) const;
		bool is_outdated() const;

		private:
		struct Symbol_db_iterator;
		std::string_view get_symbol(std::size_t index);
		std::map<std::string_view /*symbol*/, std::vector<std::filesystem::path /*lib*/> /*libs*/> get_libraries(Symbol_db_iterator begin,
																												 Symbol_db_iterator end) const;
		Symbol_db_iterator begin() const;
		Symbol_db_iterator end() const;

		const std::uint8_t *data;
		std::size_t data_size;
		std::size_t symbols{};
		std::size_t libs{};
		const std::uint8_t *symbol_db;
		const std::uint8_t *symbol_indexes;
		const std::uint8_t *lib_db;
		const std::uint8_t *lib_indexes;
		std::uint8_t lib_index_size;
		bool outdated;
	};
}; // namespace Symbol_database
