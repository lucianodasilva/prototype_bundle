#pragma once
#ifndef _STATICRINGBUFFER_H_
#define _STATICRINGBUFFER_H_

#include <memory>
#include <queue>
#include <stdexcept>
#include <type_traits>

namespace details {
	template < typename _t >
	struct aligned_ptr;
}

namespace std {

	template < typename _t >
	struct iterator_traits < details::aligned_ptr < _t > > {
		using iterator_category = std::random_access_iterator_tag;

		using difference_type = std::ptrdiff_t;
		using value_type = std::remove_cv_t < _t >;
		using pointer = _t *;
		using reference = _t &;

		using aligned_type = typename std::aligned_storage < sizeof(_t), alignof (_t) >::type;
	};

	template < typename _t >
	struct iterator_traits < details::aligned_ptr < _t const > > {
		using iterator_category = std::random_access_iterator_tag;

		using difference_type = std::ptrdiff_t;
		using value_type = _t;
		using pointer = _t const*;
		using reference = _t const&;

		using aligned_type = typename std::aligned_storage < sizeof(_t), alignof (_t) >::type const;
	};
}

namespace details {

	template<typename _t>
	struct aligned_ptr {
		using _iterator_traits 	= typename std::iterator_traits < aligned_ptr >;
	public:

		using difference_type 	= typename _iterator_traits::difference_type;
		using value_type 		= typename _iterator_traits::value_type;
		using pointer 			= typename _iterator_traits::pointer;
		using reference 		= typename _iterator_traits::reference;

		using aligned_type 		= typename _iterator_traits::aligned_type;

		inline aligned_ptr& operator += (difference_type n) noexcept {
			address += n;
			return *this;
		}

		inline aligned_ptr operator+(difference_type n) const noexcept {
			return aligned_ptr{ address + n };
		}

		inline aligned_ptr& operator -= (difference_type n) noexcept {
			address -= n;
			return *this;
		}
		inline aligned_ptr operator-(difference_type n) const noexcept {
			return aligned_ptr{ address - n };
		}

		template < typename _ct >
		inline difference_type operator - (aligned_ptr < _ct > const& b) const noexcept {
			return address - b.address;
		}

		inline aligned_ptr operator++() noexcept {
			++address;
			return aligned_ptr{ address };
		}

		inline aligned_ptr operator--() noexcept {
			++address;
			return aligned_ptr{ address };
		}

		template < typename _ct >
		inline bool operator == (aligned_ptr < _ct > const& v) const noexcept {
			return address == v.address;
		}

		template < typename _ct >
		inline bool operator != (aligned_ptr < _ct > const& v) const noexcept {
			return !(*this == v);
		}

		template < typename _ct >
		inline bool operator < (aligned_ptr < _ct > const& v) const noexcept {
			return address < v.address;
		}

		template < typename _ct >
		inline bool operator > (aligned_ptr < _ct > const& v) const noexcept {
			return address > v.address;
		}

		inline pointer get(std::size_t offset = 0) noexcept {
			return reinterpret_cast <pointer>(address + offset);
		}

		inline reference operator[](std::size_t i) noexcept {
			return *get(i);
		}

		inline reference operator*() noexcept {
			return *get();
		}

		inline pointer operator->() noexcept {
			return get();
		}

		aligned_type* address{ nullptr };

		aligned_ptr() = default;

		inline constexpr aligned_ptr(aligned_ptr const& origin)
			: address{ origin.address }
		{}

		inline aligned_ptr(aligned_ptr&& m) noexcept {
			std::swap(address, m.address);
		}

		inline explicit aligned_ptr(aligned_type* address_v)
			: address{ address_v }
		{}

		inline aligned_ptr& operator = (aligned_ptr const& v) {
			address = v.address;
			return *this;
		}

		inline aligned_ptr& operator = (aligned_ptr&& v) noexcept {
			std::swap(address, v.address);
			return *this;
		}
	};

}

template<class _t, std::size_t _n>
struct aligned_storage {
public:

	using aligned_type 			= typename details::aligned_ptr<_t>::aligned_type;

	using value_type 			= std::remove_cv_t < _t >;
	using reference 			= value_type &;
	using const_reference 		= value_type const&;

	using aligned_pointer       = details::aligned_ptr<value_type>;
	using const_aligned_pointer = details::aligned_ptr<value_type const>;

	aligned_type data[_n];

	inline const_aligned_pointer begin() const noexcept {
		return const_aligned_pointer{ +data };
	};

	inline aligned_pointer begin() noexcept {
		return aligned_pointer{ +data };
	};

	inline const_aligned_pointer end() const noexcept {
		return const_aligned_pointer{ data + _n };
	}

	inline aligned_pointer end() noexcept {
		return aligned_pointer{ data + _n };
	}
};

template<class _t, std::size_t _capacity>
struct static_ring_buffer {
private:
	using _aligned_storage_t 	= aligned_storage < _t, _capacity >;
	using _aligned_ptr_t 		= typename _aligned_storage_t::aligned_pointer;
	using _const_aligned_ptr_t 	= typename _aligned_storage_t::const_aligned_pointer;
public:

	using value_type                = _t;
	using reference                 = _t &;
	using const_reference           = _t const&;
	using pointer                   = _t *;
	using const_pointer             = _t const*;

	using size_type                 = std::size_t;

	static constexpr std::size_t 	capacity = _capacity;

	static_ring_buffer() = default;

	~static_ring_buffer() {
		apply_range(*this, &static_ring_buffer::destroy_range);
	}

	inline static_ring_buffer(static_ring_buffer const& origin) {
		operator = (origin);
	}

	inline static_ring_buffer(static_ring_buffer&& origin) noexcept {
		apply_range(
			*this,
			&static_ring_buffer::uninit_move_range,
			at(0));

		std::swap(_begin, origin._begin);
		std::swap(_count, origin, _count);
	}

	inline static_ring_buffer& operator=(static_ring_buffer const& origin) {
		apply_range(*this, &static_ring_buffer::destroy_range);

		auto begin = at(0);

		apply_range(
			origin,
			&static_ring_buffer::uninit_copy_range,
			at(0));

		_begin = 0;
		_count = origin._count;

		return *this;
	}

	/**
	* TODO: evaluate if both instances alive moving makes any sense. It should be too performance intense to be useful.
	* if in the end we decide to implement as resizeable buffer instead of static local buffer it would be much easier.
	*/

	inline bool is_full() const noexcept {
		return _count == _capacity;
	}

	inline bool empty() const noexcept {
		return _count == 0;
	}

	inline size_type size() const noexcept {
		return _count;
	}

	inline reference front() noexcept {
		return *at(0);
	}

	inline const_reference front() const noexcept {
		return *at(0);
	}

	inline reference back() noexcept {
		return *(at(_count - 1));
	}

	inline const_reference back() const noexcept {
		return *(at(_count - 1));
	}

	inline void push_back(const_reference v) {
		if (is_full())
			throw std::runtime_error("static ring buffer capacity exceeded");

		// create in place
		new (at(_count).address) value_type(v);
		++_count;
	}

	inline void push_back(value_type&& v) {
		if (is_full())
			throw std::runtime_error("static ring buffer capacity exceeded");

		// move in place
		new(at(_count).address) value_type(std::move(v));
		++_count;
	}

	template<typename ... _args_tv>
	inline void emplace_back(_args_tv&& ... args) {
		if (is_full())
			throw std::runtime_error("static ring buffer capacity exceeded");

		// move in place
		new(at(_count).address) value_type(std::forward<_args_tv>(args)...);
		++_count;
	}

	inline void pop_front() {
		if (empty())
			return;

		// destroy in place
		at(0).get()->~value_type();

		// move stuff
		++_begin;
		--_count;

		if (_begin == _capacity)
			_begin = 0;
	}

private:

	inline bool is_folded() const noexcept {
		return _begin + _count >= _capacity;
	}

	inline size_type transpose_index(size_type index) const noexcept {
		size_type offset = _begin + index;

		if (offset >= _capacity)
			offset -= _capacity;

		return offset;
	}

	inline _aligned_ptr_t at(size_type index) noexcept {
		return _data.begin() + transpose_index(index);
	}

	inline _const_aligned_ptr_t at(size_type index) const noexcept {
		return _data.begin() + transpose_index(index);
	}

	template < typename _buffer_t, typename _f, typename ... _args_tv >
	inline static void apply_range(_buffer_t& buffer, _f func, _args_tv&& ... args) {
		if (buffer.is_folded()) {
			func(
				buffer.at(0),
				buffer._data.end(),
				std::forward < _args_tv >(args)...);

			func(
				buffer._data.begin(),
				buffer.at(buffer._count),
				std::forward < _args_tv >(args)...);
		}
		else {
			func(
				buffer.at(0),
				buffer.at(buffer._count),
				std::forward < _args_tv >(args)...);
		}
	}

	inline static void destroy_range(_aligned_ptr_t b, _aligned_ptr_t e) {
		if (!std::is_pointer<_t>()) {
			while (b < e) {
				b.get()->~value_type(); // call destructor for item
				++b;
			}
		}
	}

	inline static void uninit_copy_range(_const_aligned_ptr_t b, _const_aligned_ptr_t e, _aligned_ptr_t d) {
		std::uninitialized_copy(b, e, d);
	}

	inline static void uninit_move_range(_aligned_ptr_t b, _aligned_ptr_t e, _aligned_ptr_t d) {
		//std::uninitialized_move(b, e, d); | C++ 17 only :(

		for (; b != e; ++d, (void) ++b)
			::new (static_cast<void*>(std::addressof(*d)))
			typename std::iterator_traits<_aligned_ptr_t>::value_type(std::move(*b));
	}

	_aligned_storage_t
		_data;

	std::size_t
		_begin{ 0 },
		_count{ 0 };
};

template<typename _t, std::size_t _capacity>
using static_queue = std::queue<_t, static_ring_buffer<_t, _capacity> >;

#endif