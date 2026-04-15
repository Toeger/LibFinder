#include "libclang.h"

#include <clang-c/Index.h>
#include <format>
#include <iostream>
#include <magic_enum.hpp>
#include <stdexcept>

template <>
struct magic_enum::customize::enum_range<CXCursorKind> {
	static constexpr int min = 1;
	static constexpr int max = CXCursor_OverloadCandidate;
};

template <class Pointer_t, auto creator, auto disposer, class Creator = decltype(creator), class Disposer = decltype(disposer)>
struct Resource;

template <auto creator, auto disposer, class Pointer_t, class... Args>
struct Resource<Pointer_t, creator, disposer, Pointer_t (*)(Args...), void (*)(Pointer_t)> {
	Resource() {}
	Resource(Args... args)
		: resource{creator(args...)} {}
	std::unique_ptr<std::remove_pointer_t<Pointer_t>, decltype([](Pointer_t p) { disposer(p); })> resource;
	operator Pointer_t() {
		return resource.get();
	}
};

struct Libclang::Libclang_Pimpl {
	Resource<CXIndex, clang_createIndex, clang_disposeIndex> index;
	Resource<CXTranslationUnit, clang_parseTranslationUnit, clang_disposeTranslationUnit> translation_unit;
};

Libclang::Libclang(std::filesystem::path path_, std::filesystem::path compile_commands_json_directory)
	: path{std::move(path_)}
	, pimpl{std::make_unique<Libclang_Pimpl>()} {
	pimpl->index = {0, 0};
	pimpl->translation_unit = {pimpl->index, path.c_str(), nullptr, 0, nullptr, 0, CXTranslationUnit_None};
	if (pimpl->translation_unit == nullptr) {
		throw std::runtime_error{std::format("Failed loading file {}", path.string())};
	}
}

Libclang::Libclang(Libclang &&) = default;
Libclang &Libclang::operator=(Libclang &&) = default;
Libclang::~Libclang() = default;

struct Clang_String {
	Clang_String(CXString s)
		: str{s} {}
	~Clang_String() {
		clang_disposeString(str);
	}
	Clang_String &operator=(CXString &&s) {
		clang_disposeString(str);
		str = s;
		return *this;
	}
	operator const char *() const {
		return clang_getCString(str);
	}
	std::string_view to_string_view() const {
		return static_cast<const char *>(*this);
	}
	CXString str;
};

struct Clang_String_Set {
	struct Clang_String_Set_Iterator {
		Clang_String_Set_Iterator &operator++() {
			++strings;
			return *this;
		}
		Clang_String operator*() {
			return *strings;
		}
		auto operator<=>(const Clang_String_Set_Iterator &) const = default;
		CXString *strings;
	};

	Clang_String_Set(CXStringSet *&&s)
		: strs{s} {}
	Clang_String operator[](std::size_t index) {
		assert(strs);
		assert(index < strs->Count);
		return strs->Strings[index];
	}
	~Clang_String_Set() {
		if (strs) {
			clang_disposeStringSet(strs);
		}
	}

	Clang_String_Set_Iterator begin() {
		return {strs ? strs->Strings : nullptr};
	}
	Clang_String_Set_Iterator end() {
		return {strs ? strs->Strings + strs->Count : nullptr};
	}

	operator bool() const {
		return strs;
	}

	CXStringSet *strs;
};

template <>
struct std::formatter<Clang_String, char> : std::formatter<const char *, char> {
	template <class FmtContext>
	FmtContext::iterator format(const Clang_String &s, FmtContext &ctx) const {
		return std::formatter<const char *, char>::format(+s, ctx);
	}
};

static Libclang::Location get_location(CXCursor cursor) {
	auto location = clang_getCursorLocation(cursor);
	CXFile file;
	unsigned int line;
	clang_getExpansionLocation(location, &file, &line, nullptr, nullptr);
	const auto file_name = Clang_String{clang_getFileName(file)};
	return {
		.path = file_name ? std::filesystem::weakly_canonical(file_name.to_string_view()) : std::filesystem::path{},
		.line = line,
	};
}

Libclang::Symbol_Location Libclang::get_locations(std::string_view mangled_name) {
	struct User_Data {
		Symbol_Location symbol_location;
		std::string_view mangled_name;
	} user_data{.symbol_location = {}, .mangled_name = mangled_name};
	CXCursor cursor = clang_getTranslationUnitCursor(pimpl->translation_unit);
	clang_visitChildren(
		cursor,
		[](CXCursor current_cursor, CXCursor /*parent*/, CXClientData client_data) {
			//Clang_String current_display_name = clang_getCursorDisplayName(current_cursor);
			//std::cout << "Visiting element " << Color::symbol(current_display_name) << " of type "
			//		  << Color::symbol(magic_enum::enum_name(clang_getCursorKind(current_cursor))) << '(' << clang_getCursorKind(current_cursor) << ')'
			//		  << " at " << get_location(current_cursor) << std::endl;
			switch (clang_getCursorKind(current_cursor)) {
				case CXCursor_VarDecl:
				case CXCursor_FunctionDecl: {
					if (auto display_name = Clang_String{clang_getCursorDisplayName(current_cursor)}; not display_name or not *display_name) {
						break;
					}
					const Clang_String mangled = clang_Cursor_getMangling(current_cursor);
					const auto ud = reinterpret_cast<User_Data *>(client_data);
					if (+mangled == ud->mangled_name) {
						if (not ud->symbol_location.is_definition) {
							ud->symbol_location.declaration = get_location(current_cursor);
							ud->symbol_location.is_definition = clang_isCursorDefinition(current_cursor);
						}
					}
				} break;
				case CXCursor_VariableRef:
				case CXCursor_CallExpr:
				case CXCursor_MemberRefExpr: {
					const auto ref_cursor = clang_getCursorReferenced(current_cursor);
					if (clang_equalCursors(ref_cursor, clang_getNullCursor())) {
						return CXChildVisit_Recurse;
					}
					const Clang_String mangled = clang_Cursor_getMangling(ref_cursor);
					//std::cout << "Mangled name: " << Color::symbol(mangled) << " at " << get_location(current_cursor) << '\n';
					//std::cout << "Demangled name: " << Color::symbol(Symbol{Symbol_type::undefined, +mangled}.demangled_name()) << '\n';
					auto ud = reinterpret_cast<User_Data *>(client_data);
					if (+mangled == ud->mangled_name) {
						if (not ud->symbol_location.is_definition) {
							ud->symbol_location.declaration = get_location(ref_cursor);
							ud->symbol_location.is_definition = clang_isCursorDefinition(ref_cursor);
						}
						ud->symbol_location.usage = get_location(current_cursor);
					}
				} break;
				default:
					break;
			}
			return CXChildVisit_Recurse;
		},
		&user_data);
	return std::move(user_data.symbol_location);
}
