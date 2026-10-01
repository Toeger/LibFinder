#pragma once

#include <cstddef>

struct Bimpl_Info {
	std::size_t size = sizeof(void *[16]);
	std::size_t alignment = alignof(void *);
	//only 1 of the following 4 should be set
	bool nothrow_copyable = false; //implies nothrow_movable
	bool nothrow_movable = false;
	bool throw_copyable = false; //implies throw_movable
	bool throw_movable = false;
	bool wasteless = false;		  //static_assert that size and alignment are an exact fit and noexcept is not missing
	bool allow_terminate = false; //allows noexcept copy/move Bimpl for types that are not noexcept copy/move
};

template <class Impl, Bimpl_Info pimpl_info, class... Impl_Params>
struct Bimpl {
	Bimpl(Impl_Params... params);
	Bimpl(const Bimpl &) noexcept
		requires(pimpl_info.nothrow_copyable);
	Bimpl(const Impl &) noexcept
		requires(pimpl_info.nothrow_copyable);
	Bimpl &operator=(const Bimpl &) noexcept
		requires(pimpl_info.nothrow_copyable);
	Bimpl &operator=(const Impl &) noexcept
		requires(pimpl_info.nothrow_copyable);
	Bimpl(Bimpl &&) noexcept
		requires(pimpl_info.nothrow_movable or pimpl_info.nothrow_copyable);
	Bimpl(Impl &&) noexcept
		requires(pimpl_info.nothrow_movable or pimpl_info.nothrow_copyable);
	Bimpl &operator=(Bimpl &&) noexcept
		requires(pimpl_info.nothrow_movable or pimpl_info.nothrow_copyable);
	Bimpl &operator=(Impl &&) noexcept
		requires(pimpl_info.nothrow_movable or pimpl_info.nothrow_copyable);
	Bimpl(const Bimpl &)
		requires(pimpl_info.throw_copyable);
	Bimpl(const Impl &)
		requires(pimpl_info.throw_copyable);
	Bimpl &operator=(const Bimpl &)
		requires(pimpl_info.throw_copyable);
	Bimpl &operator=(const Impl &)
		requires(pimpl_info.throw_copyable);
	Bimpl(Bimpl &&)
		requires(pimpl_info.throw_movable or pimpl_info.throw_copyable);
	Bimpl(Impl &&)
		requires(pimpl_info.throw_movable or pimpl_info.throw_copyable);
	Bimpl &operator=(Bimpl &&)
		requires(pimpl_info.throw_movable or pimpl_info.throw_copyable);
	Bimpl &operator=(Impl &&)
		requires(pimpl_info.throw_movable or pimpl_info.throw_copyable);
	~Bimpl();
	operator Impl &() &;
	operator Impl &&() &&;
	operator const Impl &() const;
	Impl &operator+() &;
	Impl &&operator+() &&;
	const Impl &operator+() const;

	private:
	alignas(pimpl_info.alignment) char _[pimpl_info.size];
};

#undef TEMPLATE
#undef PIMPL
