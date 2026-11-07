#pragma once

#include "bimpl.h"
#include "iterable.h"

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
		struct Libraries_Of_Symbol_Iterable;
		struct Symbols_Libraries_Of_Prefix_Iterable;

		public:
		Reader(std::filesystem::path path = data_base_filepath);
		~Reader();
		Reader(Reader &&other);
		Reader &operator=(Reader &&other);

		Libraries_Of_Symbol_Iterable libs(std::string_view symbol);
		Symbols_Libraries_Of_Prefix_Iterable symbol_libs(std::string_view symbol_prefix);

		bool is_outdated() const;

		private:
		struct Symbol_Db_Iterator;
		std::string_view get_symbol(std::size_t index) const;
		Symbol_Db_Iterator begin() const;
		Symbol_Db_Iterator end() const;

		std::basic_string_view<std::byte> data;
		std::uint32_t symbol_count{};
		std::uint8_t sizeof_Lib_Index;
		std::basic_string_view<std::byte> mangled_symbol_indexes;
		std::basic_string_view<std::byte> lib_indexes;
		bool outdated;

		struct Libraries_Of_Symbol_Impl;
		friend Libraries_Of_Symbol_Impl;
		static constexpr Bimpl_Info libraries_of_symbol_pimpl_info{.size = 56, .nothrow_copyable = true, .wasteless = true};
		struct Libraries_Of_Symbol_Iterable : Bimpl<Libraries_Of_Symbol_Impl, libraries_of_symbol_pimpl_info, Symbol_database::Reader &, std::string_view>,
											  Iterable {
			void assign_next(std::optional<std::filesystem::path> &next);
		};

		struct Libraries_Of_Prefix_Impl;
		friend Libraries_Of_Prefix_Impl;
		static constexpr Bimpl_Info libraries_of_prefix_pimpl_info{.size = 112, .nothrow_copyable = true, .wasteless = true};
		struct Symbols_Libraries_Of_Prefix_Iterable
			: Bimpl<Libraries_Of_Prefix_Impl, libraries_of_prefix_pimpl_info, Symbol_database::Reader &, std::string_view>,
			  Iterable {
			void assign_next(std::optional<std::pair<std::string_view /*symbol*/, std::filesystem::path>> &next);
		};
	};
}; // namespace Symbol_database

extern template struct Bimpl<Symbol_database::Reader::Libraries_Of_Symbol_Impl, Symbol_database::Reader::libraries_of_symbol_pimpl_info,
							 Symbol_database::Reader &, std::string_view>;

extern template struct Bimpl<Symbol_database::Reader::Libraries_Of_Prefix_Impl, Symbol_database::Reader::libraries_of_prefix_pimpl_info,
							 Symbol_database::Reader &, std::string_view>;
