#pragma once

#include <cassert>
#include <optional>

void iterable_tester();

namespace detail::iterable {
	namespace helper {
		template <class Class, class Product>
		[[maybe_unused]] Product generator_producer(void (Class::*)(std::optional<Product> &));

		template <class Class, class Product>
		[[maybe_unused]] Product generator_producer(std::optional<Product> (Class::*)());

		template <class Class>
		auto has_get_next(Class &) -> decltype(&Class::get_next, std::true_type{});
		std::false_type has_get_next(...);

		template <class Class>
		auto has_assign_next(Class &) -> decltype(&Class::assign_next, std::true_type{});
		std::false_type has_assign_next(...);
	} // namespace helper

	template <auto producer>
	using Product = decltype(helper::generator_producer(producer));

	template <class Class>
	constexpr bool has_get_next = decltype(detail::iterable::helper::has_get_next(std::declval<Class>()))::value;

	template <class Class>
	constexpr bool has_assign_next = decltype(detail::iterable::helper::has_assign_next(std::declval<Class>()))::value;

	template <class Class>
	concept Iterable_Compatible = requires { has_get_next<Class> or has_assign_next<Class>; };
} // namespace detail::iterable

struct Iterable {
	template <detail::iterable::Iterable_Compatible State>
	struct Iterable_Iterator {
		using difference_type = int;
		using value_type = decltype(static_cast<State *>(nullptr)->get_next())::value_type;

		Iterable_Iterator(State &s)
			: state{&s} {
			store_next_product();
		}

		Iterable_Iterator &operator++() {
			store_next_product();
			return *this;
		}

		Iterable_Iterator operator++(int) {
			Iterable_Iterator result{*state};
			result.product = std::move(product);
			store_next_product();
			return result;
		}

		value_type &operator*() const {
			assert(product.has_value());
			return *product;
		}

		friend bool operator==(const Iterable_Iterator &it, std::default_sentinel_t) {
			return not it.product.has_value();
		}
		friend bool operator==(std::default_sentinel_t, const Iterable_Iterator &it) {
			return not it.product.has_value();
		}
		friend bool operator!=(const Iterable_Iterator &it, std::default_sentinel_t) {
			return it.product.has_value();
		}
		friend bool operator!=(std::default_sentinel_t, const Iterable_Iterator &it) {
			return it.product.has_value();
		}

		private:
		void store_next_product() {
			state->assign_next(product);
		}

		mutable std::optional<value_type> product = std::nullopt;
		State *state;
	};

	template <detail::iterable::Iterable_Compatible Generator>
	auto begin(this Generator &self)
		requires(not std::is_same_v<Generator, Iterable>)
	{
		return Iterable_Iterator<Generator>{self};
	}

	static std::default_sentinel_t end() {
		return {};
	}

	template <class Container, detail::iterable::Iterable_Compatible Generator>
	operator Container(this Generator &self)
		requires(std::constructible_from<Container, std::from_range_t, Generator>)
	{
		return {std::from_range, self};
	}

	template <class Container, detail::iterable::Iterable_Compatible Generator>
	operator Container(this Generator &&self)
		requires(std::constructible_from<Container, std::from_range_t, Generator>)
	{
		return {std::from_range, self};
	}

	template <detail::iterable::Iterable_Compatible Generator>
		requires(not detail::iterable::has_assign_next<Generator>)
	void assign_next(this Generator &self, std::optional<detail::iterable::Product<&Generator::get_next>> &next) {
		next = self.get_next();
	}

	template <detail::iterable::Iterable_Compatible Generator>
		requires(not detail::iterable::has_get_next<Generator>)
	std::optional<detail::iterable::Product<&Generator::assign_next>> get_next(this Generator &self) {
		std::optional<detail::iterable::Product<&Generator::assign_next>> next;
		self.assign_next(next);
		return next;
	}
};

template <class Transformer, class Source>
struct Transformed : Iterable {
	auto get_next() {
		if (auto next = source->get_next(); next.has_value()) {
			return std::optional{(*transformer)(std::move(*next))};
		} else {
			return std::optional<decltype((*transformer)(std::move(*next)))>{};
		}
	}
	Transformer *transformer;
	Source *source;
};

template <std::derived_from<Iterable> Source, class Function>
auto operator|(Source &&source, Function &&f)
	requires(not std::is_same_v<std::remove_cvref_t<Source>, Iterable> and std::is_invocable_v<decltype(f), decltype(std::move(*source.get_next()))> and
			 not std::is_void_v<std::invoke_result_t<decltype(f), decltype(std::move(*source.get_next()))>>)
{
	return Transformed{.transformer = &f, .source = &source};
}

//TODO: Add fold, skip, filter, unique, with_index, first, second
