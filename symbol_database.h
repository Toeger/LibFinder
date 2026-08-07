#pragma once

#include "utility.h"
#include <filesystem>
#include <functional>
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
		static std::pair<Writer, std::vector<std::filesystem::path>> from_db_file(
			std::filesystem::path db_file, std::function_ref<bool(std::string_view symbol, std::filesystem::path library)> filter =
											   [](std::string_view, std::filesystem::path) { return true; });
		void add(std::string mangled_symbol, std::size_t lib_id);
		Write_stats write(std::filesystem::path output_file, const std::vector<std::filesystem::path> &libraries);
		void merge(Writer &&other);
		std::size_t symbol_count() const;

		private:
		std::vector<std::vector<std::pair<std::string /*mangled_symbol*/, std::size_t /*libindex*/>>> mangled_data{{}};
		std::uint8_t sizeof_Lib_Index;
	};

	struct Reader {
		Reader(std::filesystem::path path);
		~Reader();
		Reader(Reader &&other);
		Reader &operator=(Reader &&other);
		void libraries_from_symbol(std::string symbol, std::function_ref<void(std::filesystem::path)> callback) const;
		void libraries_from_prefix(std::string symbol_prefixes, std::function_ref<void(std::string_view symbol, std::filesystem::path lib)> callback) const;
		bool is_outdated() const;

		private:
		struct Symbol_Db_Iterator;
		std::string_view get_symbol(std::size_t index) const;
		void get_libraries(Symbol_Db_Iterator begin, Symbol_Db_Iterator end,
						   Callback_Function<std::string_view /*mangled_symbol*/, std::filesystem::path /*lib*/> auto &&callback) const;
		Symbol_Db_Iterator begin() const;
		Symbol_Db_Iterator end() const;

		std::basic_string_view<std::byte> data;
		std::uint32_t symbol_count{};
		std::uint8_t sizeof_Lib_Index;
		std::size_t lib_count{};
		std::basic_string_view<std::byte> mangled_symbol_indexes;
		std::basic_string_view<std::byte> lib_indexes;
		bool outdated;
	};
}; // namespace Symbol_database
