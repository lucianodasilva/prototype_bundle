
template < typename _t, std::size_t >
using opaque_t = _t;

template < typename _t, typename _sequence_t >
struct static_initializer_list_base;

template < typename _t, std::size_t ... _sequence_v >
struct static_initializer_list_base < _t, std::index_sequence < _sequence_v... > > {
public:

	inline static_initializer_list_base (opaque_t < _t, _sequence_v > && ... args) :
		content { args... }
	{}

	constexpr static inline std::size_t size () {
		return sizeof ... (_sequence_v);
	}

	constexpr inline auto * begin () {
		return +content;
	}

	constexpr inline auto * end () {
		return content + size ();
	}

	_t content [ size () ];
};

template <
	typename _t,
	std::size_t _size,
	typename _base_t = static_initializer_list_base < _t, std::make_index_sequence < _size > >
>
struct static_initializer_list : public _base_t {
public:
	using _base_t::_base_t;
};
