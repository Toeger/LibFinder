#pragma once

#include <cassert>
#include <optional>

namespace detail::iterable {
	namespace helper {
		template <class Class, class Product>
			requires(not std::is_const_v<Class>)
		[[maybe_unused]] Product generator_producer(void (Class::*)(std::optional<Product> &));

		template <class Class, class Product>
		[[maybe_unused]] Product generator_producer(void (Class::*)(std::optional<Product> &) const);

		template <class Class, class Product>
			requires(not std::is_const_v<Class>)
		[[maybe_unused]] Product generator_producer(std::optional<Product> (Class::*)());

		template <class Class, class Product>
		[[maybe_unused]] Product generator_producer(std::optional<Product> (Class::*)() const);

		template <class Value_type>
		Value_type optional_value_type(const std::optional<Value_type> &);
	} // namespace helper

	template <class Class, auto producer>
	using Product = decltype(helper::generator_producer<std::remove_reference_t<Class>>(producer));

	template <class Class>
	concept has_direct_get_next =
		requires(Class &c) { static_cast<decltype(c.get_next()) (std::remove_reference_t<Class>::*)()>(&std::remove_reference_t<Class>::get_next); };

	template <class Class>
	concept has_direct_assign_next = requires {
		static_cast<void (std::remove_reference_t<Class>::*)(
			std::optional<decltype(helper::generator_producer<std::remove_reference_t<Class>>(&std::remove_reference_t<Class>::assign_next))> &)>(
			&std::remove_reference_t<Class>::assign_next);
	};

	template <class Class>
	concept Iterable_compatible = has_direct_assign_next<Class> or has_direct_get_next<Class>;

	template <class VT>
	using Optional_value_type = decltype(helper::optional_value_type(std::declval<VT>()));
} // namespace detail::iterable

struct Iterable {
	template <detail::iterable::Iterable_compatible State>
	struct Iterable_Iterator {
		using difference_type = int;
		using real_value_type = detail::iterable::Optional_value_type<decltype(static_cast<State *>(nullptr)->get_next())>;
		using value_type = std::remove_reference_t<real_value_type>;
		static_assert(std::is_move_constructible_v<value_type>);
		static_assert(std::is_move_assignable_v<value_type &>);

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

		mutable std::optional<real_value_type> product = std::nullopt;
		State *state;
	};

	template <detail::iterable::Iterable_compatible Generator>
	auto begin(this Generator &&self) {
		return Iterable_Iterator<std::remove_reference_t<Generator>>{self};
	}

	static std::default_sentinel_t end() {
		return {};
	}

	template <class Container>
	operator Container(this detail::iterable::Iterable_compatible auto &&self)
		requires(std::constructible_from<Container, std::from_range_t, decltype(self)>)
	{
		return {std::from_range, self};
	}

	template <detail::iterable::has_direct_get_next Generator>
	void assign_next(this Generator &&self, std::optional<detail::iterable::Product<Generator, &std::remove_reference_t<Generator>::get_next>> &next) {
		next = self.get_next();
	}

	template <detail::iterable::has_direct_assign_next Generator>
	auto get_next(this Generator &&self) {
		std::optional<detail::iterable::Product<Generator, &std::remove_reference_t<Generator>::assign_next>> next;
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
	requires(not std::is_same_v<std::remove_cvref_t<Source>, Iterable> and std::is_invocable_v<decltype(f), decltype(*source.get_next())> and
			 not std::is_void_v<std::invoke_result_t<decltype(f), decltype(*source.get_next())>>)
{
	return Transformed{.transformer = &f, .source = &source};
}

struct Limit {
	std::size_t limit;
};

static inline Limit limit(std::size_t limit) {
	return {.limit = limit};
}

template <class Source>
struct Limited : Iterable {
	Limited(Source &source_, std::size_t limit_)
		: source{&source_}
		, limit{limit_} {}
	void assign_next(decltype(std::declval<Source>().get_next()) &result) {
		if (limit == 0) {
			result = std::nullopt;
			return;
		}
		limit--;
		source->assign_next(result);
	}
	Source *source;
	std::size_t limit;
};

template <class Source>
Limited(Source &&, std::size_t) -> Limited<std::remove_reference_t<Source>>;

template <std::derived_from<Iterable> Source>
auto operator|(Source &&source, Limit limit)
	requires(not std::is_same_v<std::remove_cvref_t<Source>, Iterable>)
{
	return Limited{source, limit.limit};
}

struct {
} static inline iterable;

template <class Container>
concept is_iterable = requires(Container &container) {
	container.begin();
	container.end();
	typename std::iterator_traits<decltype(container.begin())>::value_type;
};

template <is_iterable Source>
struct Make_iterable : Iterable {
	Make_iterable(Source &source_)
		: source{&source_}
		, iterator{source_.begin()} {}

	void assign_next(std::optional<decltype(*std::declval<Source &>().begin())> &result) {
		if (iterator == source->end()) {
			result = std::nullopt;
			return;
		}
		result = *iterator;
		++iterator;
	}

	Source *source;
	decltype(std::declval<Source &>().begin()) iterator;
};

[[maybe_unused]] auto operator|(is_iterable auto &&source, decltype(iterable)) {
	return Make_iterable{source};
}

//TODO: Add fold, skip, filter, unique, with_index, first, second
