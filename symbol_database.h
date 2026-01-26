#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Symbol_database {
	struct Writer {
		void add(std::string symbol, std::string library);
		void write(std::filesystem::path path) const;
		void merge(Writer &&other);

		private:
		std::vector<std::vector<std::pair<std::string /*symbol*/, std::string /*library*/>>> data{{}};
	};

	struct Reader {
		Reader(std::filesystem::path path);
		~Reader();
		Reader(Reader &&other);
		Reader &operator=(Reader &&other);
		std::vector<std::string_view> get_libraries(std::string_view symbol, bool prefix_search = false) const;

		private:
		struct Symbol_db_iterator;
		std::string_view get_symbol(std::size_t index);
		std::vector<std::string_view> get_libraries(Symbol_db_iterator begin, Symbol_db_iterator end) const;
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
		std::uint8_t symbol_index_size;
		std::uint8_t lib_index_size;
	};
}; // namespace Symbol_database
