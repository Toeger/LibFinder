#pragma once

#include "bimpl.h"
#include "input_stream.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

extern const std::filesystem::path data_base_filepath;

namespace Symbol_database {
	struct Write_stats {
		std::size_t unique_symbols;
		std::size_t symbols_db_size;
		std::size_t libs_db_size;
		std::size_t symbols_index_size;
		std::size_t libs_index_size;
	};

	struct Writer {
		static std::pair<Writer, std::vector<std::filesystem::path>> from_db_file(
			std::function_ref<bool(std::string_view symbol, std::filesystem::path library)> filter = [](std::string_view,
																										std::filesystem::path) { return true; },
			std::filesystem::path db_file = data_base_filepath);
		void add(std::string mangled_symbol, std::size_t lib_id);
		Write_stats write(const std::vector<std::filesystem::path> &libraries, std::filesystem::path output_file = data_base_filepath);
		void merge(Writer &&other);
		std::size_t symbol_count() const;

		private:
		std::vector<std::vector<std::pair<std::string /*mangled_symbol*/, std::size_t /*libindex*/>>> mangled_data{{}};
	};

	struct Reader {
		private:
		struct Libraries_Of_Symbol_State;
		friend Libraries_Of_Symbol_State;
		static constexpr Bimpl_Info libraries_of_symbol_pimpl_info{.size = 56, .nothrow_copyable = true};
		static void libraries_of_symbol(std::optional<std::filesystem::path> &result, Libraries_Of_Symbol_State &state);
		static_assert(input_stream_compatible<&libraries_of_symbol>);

		struct Libraries_Of_Prefix_State;
		friend Libraries_Of_Prefix_State;
		static constexpr Bimpl_Info libraries_of_prefix_pimpl_info{.size = 112, .nothrow_copyable = true};
		static void libraries_of_prefix(std::optional<std::pair<std::string_view /*symbol*/, std::filesystem::path>> &result, Libraries_Of_Prefix_State &state);
		static_assert(input_stream_compatible<&libraries_of_prefix>);

		public:
		Reader(std::filesystem::path path = data_base_filepath);
		~Reader();
		Reader(Reader &&other);
		Reader &operator=(Reader &&other);

		Input_Stream<std::filesystem::path,
					 Bimpl<Libraries_Of_Symbol_State, libraries_of_symbol_pimpl_info, Symbol_database::Reader &, std::string_view /*symbol*/>,
					 libraries_of_symbol>
		libs(std::string_view symbol);

		Input_Stream<std::pair<std::string_view /*symbol*/, std::filesystem::path>,
					 Bimpl<Libraries_Of_Prefix_State, libraries_of_prefix_pimpl_info, Symbol_database::Reader &, std::string_view /*symbol prefix*/>,
					 libraries_of_prefix>
		symbol_libs(std::string_view symbol_prefix);

		void libraries_from_symbol(std::string_view symbol, std::function_ref<void(std::filesystem::path)> callback) const;
		void libraries_from_prefix(std::string symbol_prefixes, std::function_ref<void(std::string_view symbol, std::filesystem::path lib)> callback) const;
		bool is_outdated() const;

		private:
		struct Symbol_Db_Iterator;
		std::string_view get_symbol(std::size_t index) const;
		void get_libraries(Symbol_Db_Iterator begin, Symbol_Db_Iterator end,
						   std::invocable<std::string_view /*mangled_symbol*/, std::filesystem::path /*lib*/> auto &&callback) const;
		Symbol_Db_Iterator begin() const;
		Symbol_Db_Iterator end() const;

		std::basic_string_view<std::byte> data;
		std::uint32_t symbol_count{};
		std::uint8_t sizeof_Lib_Index;
		std::basic_string_view<std::byte> mangled_symbol_indexes;
		std::basic_string_view<std::byte> lib_indexes;
		bool outdated;
	};
}; // namespace Symbol_database
