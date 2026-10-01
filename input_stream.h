#pragma once

#include <cassert>
#include <optional>

template <class State, class Product, auto producer>
struct Input_Stream_Iterator {
	Input_Stream_Iterator(State &s)
		: state{s} {
		get_next_product();
	}

	Input_Stream_Iterator &operator++() {
		get_next_product();
		return *this;
	}

	Product &operator*() {
		assert(product.has_value());
		return *product;
	}

	bool operator!=(std::default_sentinel_t) const {
		return product.has_value();
	}

	private:
	void get_next_product() {
		producer(product, state);
	}

	std::optional<Product> product = std::nullopt;
	State &state;
};

template <class Product, class State, auto producer>
class Input_Stream {
	public:
	Input_Stream() = default;
	template <class... State_Params>
	Input_Stream(State_Params &&...state_params)
		: state{std::forward<decltype(state_params)>(state_params)...} {}

	Input_Stream_Iterator<State, Product, producer> begin() {
		return {state};
	}

	std::default_sentinel_t end() {
		return {};
	}

	private:
	State state;

	friend Input_Stream_Iterator<State, Product, producer>;
};

namespace detail::Input_Stream {
	namespace helper {
		template <class State, class Product>
		using Producer_Signature = void (*)(std::optional<Product> &, State);
		template <class State, class Product, class Class>
		using Member_Producer_Signature = void (Class::*)(std::optional<Product> &, State);
		template <class State, class Product, class Class>
		using Const_Member_Producer_Signature = void (Class::*)(std::optional<Product> &, State) const;

		template <class State, class Product>
		[[maybe_unused]] std::true_type is_input_stream_producer_helper(Producer_Signature<State, Product>);
		[[maybe_unused]] std::false_type is_input_stream_producer_helper(...);

		template <class State, class Product, class Class>
		[[maybe_unused]] std::true_type is_member_input_stream_producer_helper(Member_Producer_Signature<State, Product, Class>);
		template <class State, class Product, class Class>
		[[maybe_unused]] std::true_type is_member_input_stream_producer_helper(Const_Member_Producer_Signature<State, Product, Class>);
		[[maybe_unused]] std::false_type is_member_input_stream_producer_helper(...);

		template <class State, class Product>
		[[maybe_unused]] State input_stream_producer_state_helper(Producer_Signature<State, Product>);
		template <class State, class Product, class Class>
		[[maybe_unused]] State input_stream_producer_state_helper(Member_Producer_Signature<State, Product, Class>);
		template <class State, class Product, class Class>
		[[maybe_unused]] State input_stream_producer_state_helper(Const_Member_Producer_Signature<State, Product, Class>);

		template <class State, class Product>
		[[maybe_unused]] Product input_stream_producer_product_helper(Producer_Signature<State, Product>);
		template <class State, class Product, class Class>
		[[maybe_unused]] Product input_stream_producer_product_helper(Member_Producer_Signature<State, Product, Class>);
		template <class State, class Product, class Class>
		[[maybe_unused]] Product input_stream_producer_product_helper(Const_Member_Producer_Signature<State, Product, Class>);

		template <class State, class Product, class Class>
		[[maybe_unused]] Class input_stream_producer_class_helper(Member_Producer_Signature<State, Product, Class>);
		template <class State, class Product, class Class>
		[[maybe_unused]] const Class input_stream_producer_class_helper(Const_Member_Producer_Signature<State, Product, Class>);
	} // namespace helper

	template <auto producer>
	constexpr bool is_input_stream_producer = decltype(helper::is_input_stream_producer_helper(producer))::value;

	template <auto producer>
	constexpr bool is_member_input_stream_producer = decltype(helper::is_member_input_stream_producer_helper(producer))::value;

	template <auto producer>
	using input_stream_producer_state_type = decltype(helper::input_stream_producer_state_helper(producer));

	template <auto producer>
	using input_stream_producer_product_type = decltype(helper::input_stream_producer_product_helper(producer));

	template <auto producer>
	using input_stream_producer_class_type = decltype(helper::input_stream_producer_class_helper(producer));
} // namespace detail::Input_Stream

template <auto producer>
concept input_stream_compatible = detail::Input_Stream::is_input_stream_producer<producer> or detail::Input_Stream::is_member_input_stream_producer<producer>;

template <auto producer,																						   //
		  class... State_Params,																				   //
		  class State = std::remove_reference_t<detail::Input_Stream::input_stream_producer_state_type<producer>>, //
		  class Product = detail::Input_Stream::input_stream_producer_product_type<producer>					   //
		  >
	requires(detail::Input_Stream::is_input_stream_producer<producer>)
Input_Stream<Product, State, producer> input_stream_for(State_Params &&...state_params) {
	return {std::forward<decltype(state_params)>(state_params)...};
}

template <class State, class Class>
struct Class_Wrapper {
	State state;
	Class object;
};

template <auto producer,																						   //
		  class... State_Params,																				   //
		  class Product = detail::Input_Stream::input_stream_producer_product_type<producer>,					   //
		  class State = std::remove_reference_t<detail::Input_Stream::input_stream_producer_state_type<producer>>, //
		  class Class = detail::Input_Stream::input_stream_producer_class_type<producer>,						   //
		  class Wrapper_State = Class_Wrapper<State, Class &>													   //
		  >
	requires(detail::Input_Stream::is_member_input_stream_producer<producer>)
Input_Stream<Product, Wrapper_State, [](Wrapper_State &state) { return (state.object.*producer)(state.state); }>
input_stream_for(Class &object, State_Params &&...state_params) {
	return {Wrapper_State{.state = State{state_params...}, .object = object}};
}

template <class Internal_State, class Product>
struct State_Wrapper {
	std::remove_reference_t<Internal_State> state;
	std::optional<Product> (*function)(Internal_State);
};

template <class Product, class State, class... State_Args>
Input_Stream<Product, State_Wrapper<State, Product>, [](State_Wrapper<State, Product> &state) { return state.function(state.state); }>
input_stream_for(std::optional<Product> (*function)(State), State_Args &&...state_args) {
	return {{.state = State(std::forward<State_Args>(state_args)...), .function = function}};
}

void input_stream_tester();
