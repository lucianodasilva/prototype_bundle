#include <benchmark/benchmark.h>

#include <deque>
#include <memory>
#include <limits>
#include <stack>
#include <malloc.h>
#include <zconf.h>
#include <iostream>
#include <cstring>

namespace memory {

	namespace literals {
		constexpr std::size_t operator ""_kb(unsigned long long v) {
			return v << 10;
		}

		constexpr std::size_t operator ""_mb(unsigned long long v) {
			return v << 20;
		}
	}

	struct node_flags {
		bool marked : 1;
		bool pinned : 1;
	};

	using page_length  = uint32_t;
	using table_address = uint32_t;

	using page_offset = uint32_t;
	using page_id = uint8_t;

	struct page_address {
		page_offset	offset 	: 24;
		page_id		page 	: 8;
	};

	struct table_object {
		page_address 		address;
		table_address 		first_ref;
		node_flags			flags;
	};

	struct table_ref {
		table_address 		to_object;
	};

	struct table_node {
		table_address 		next;

		union {
			table_object 	obj;
			table_ref 		ref;
		};
	};

	struct page_header {
		void (*destructor)(uint8_t *);
		page_offset 		next;
		page_length			length;
		table_address		node;

		inline uint8_t * begin () { return reinterpret_cast < uint8_t * > (this) + sizeof (page_header); }
		inline uint8_t * end () { begin () + length; }

		inline void dispose () {
			(*destructor)(begin());
		}
	};

	struct page_header_allocated {
		page_header & 	header;
		page_address 	address;
	};

	struct table {
	public:

		table () {
			grow_table (512);
		}

		inline table_node & root () { return _data[0]; }

		table_address add_obj () {
			return reg (root().next);
		}

		table_address add_ref (table_address from_object, table_address to_object) {

			auto index = reg (_data [from_object].obj.first_ref);
			_data [index].ref.to_object = to_object;
			return index;
		}

		void delete_obj (table_address object) {
			// clear refs
			auto & node = get(object);

			table_address next = node.obj.first_ref;

			while (next) {
				auto rem = next;
				next = get(rem).next;
				release(rem);
			}

			remove (root().next, object);
		}

		void delete_ref (table_address from_object, table_address ref) {
			remove (_data[from_object].obj.first_ref, ref);
		}

		table_node & get (table_address node) {
			return _data [node];
		}

		struct enumerator {
			table_address address {0};

			inline explicit operator bool () const { return address != 0; }

			inline table_node & current (table * t) {
				return t->_data [address];
			}

			inline bool next (table const * t) {
				address = t->_data [address].next;
				return address != 0;
			}
		};

	private:
		std::vector < table_node >	_data;

		table_address				_free_head {1},
									_free_tail {1};

		inline table_address reg (table_address & root_next_ptr) {
			auto index = reserve();
			insert (root_next_ptr, index);
			return index;
		}

		inline void insert (table_address & root_next_ptr, table_address address) {
			auto & node 	= _data [address];

			node.next = root_next_ptr;

			root_next_ptr = address;
		}

		inline void remove (table_address & root_next_ptr, table_address address) {
			enumerator en { root_next_ptr };

			if (en.address == 0)
				return;

			if (root_next_ptr == address) {
				auto & item = en.current(this);
				root_next_ptr = item.next;
			} else {
				do {
					auto &item = en.current(this);

					if (item.next == address) {
						item.next = _data[address].next;
						break;
					}
				} while (en.next(this));
			}

			release (address);
		}

		inline table_address reserve () {
			auto index = _free_head;

			_free_head = _data[index].next;
			if (_free_head == 0) {
				_free_head = _free_tail;
				grow_table (_data.size () * 2);
			}

			// clear node
			_data [index] = {};

			return index;
		}

		inline void release (table_address address) {
			_data [address].next = _free_head;
			_free_head = address;
		}

		void grow_table (std::size_t new_size) {
			if (_data.size() > new_size)
				return;

			auto index = _data.size();
			_data.resize(new_size);

			// ignore root
			if (index == 0)
				index = 1;

			while (index < new_size) {
				_data [_free_tail].next = index;
				_free_tail = index;
				++index;
			}

			_data [_free_tail].next = 0;
		}

	};

	template < std::size_t capacity >
	struct page {
	public:

		struct enumerator {
			page_offset offset {0};

			inline explicit operator bool () const { return offset != 0; }

			inline page_header * current (page & p) {
				return reinterpret_cast  < page_header * > (p._buffer + offset);
			}

			inline bool next (page & p) {
				offset = current (p)->next;
				return offset != 0;
			}
		};

		page () = default;

		inline page (page && p) noexcept {
			swap (p);
		}

		inline page & operator = (page && p) noexcept {
			swap (p);
			return *this;
		}

		~page(){
			free(_buffer);
		}

		void swap (page & p) noexcept {
			using std::swap;
			swap (_buffer, p._buffer);
			swap (_offset, p._offset);
		}

		void reset () {
			_offset = 0;
		}

		bool is_valid (page_offset offset) const noexcept {
			return offset < _offset;
		}

		bool has_capacity (page_length length) const noexcept {
			return capacity > (_offset + length);
		}

		bool empty () const noexcept {
			return _offset == 0;
		}

		page_header_allocated allocate (page_length length) {
			auto offset = _offset;
			_offset += length + sizeof(page_header); // TODO: automatically align perhaps?

			auto * header = get_header (offset);

			header->next = _offset;
			header->length = length;

			return { *header, {offset} };
		}

		page_header_allocated move_allocated (page_header & header) {
			auto alloc = allocate (header.length);

			alloc.header.node = header.node;
			alloc.header.destructor = header.destructor;

			std::copy(header.begin(), header.end(), alloc.header.begin());

			return alloc;
		}

		inline page_header * get_header (page_offset offset) {
			return reinterpret_cast < page_header * >  (_buffer + offset);
		}

		template < typename _t >
		_t * get_ptr (page_offset offset) {
			return reinterpret_cast < _t * > (_buffer + offset + sizeof(page_header));
		}

	private:
		uint8_t * 		_buffer { reinterpret_cast < uint8_t * > (memalign (sysconf (_SC_PAGESIZE), capacity)) };
		page_offset 	_offset { 0 };
	};

	template < std::size_t page_size >
	struct paging {

		using page_type = page < page_size >;

		page_type					work_page;
		std::vector < page_type > 	pages;

		paging () {
			// add active page
			pages.emplace_back ();
		}

		bool is_valid (page_address address) const noexcept {
			return pages.size() < address.page && pages[address.page].is_valid(address.offset);
		}

		inline page_header * get_header (page_address address) {
			return pages [address.page].get_header (address.offset);
		}

		template < typename _t >
		inline _t * get_ptr (page_address address) {
			return pages [address.page].template get_ptr < _t > (address.offset);
		}

		page_header_allocated allocate (page_length length) {
			if (!pages.back ().has_capacity (length)) {
				pages.emplace_back();
			}

			auto alloc = pages.back ().allocate (length);
			alloc.address.page = pages.size() - 1;

			return alloc;
		}

		void compress (table & table) {

			uint32_t recycled_page = 0;
			work_page.reset();

			for (auto & page : pages) {
				if (page.empty())
					continue;

				typename page_type::enumerator en {};

				do {
					if (!work_page.has_capacity(en.current(page)->length)) {
						std::swap (pages[recycled_page], work_page);
						work_page.reset();
						++recycled_page;
					}

					auto * header = en.current(page);

					if (table.get(header->node).obj.flags.marked) {
						// move from one page to another if marked
						auto alloc = work_page.move_allocated(*header);
						alloc.address.page = recycled_page;
						// update new allocated address
						table.get(header->node).obj.address = alloc.address;
					} else {
						// delete object
						header->dispose();
					}

				} while (en.next(page));

				page.reset();
			}

			std::swap (pages[recycled_page], work_page);
		}
	};

	template < std::size_t page_capacity >
	struct collector {
	public:

		template < typename _t, typename ... _args_tv >
		inline table_address allocate (_args_tv && ... args) {
			auto table_index = _table.add_obj();

			auto alloc = _paging.allocate (sizeof(_t));

			// set destructor
			alloc.header.destructor = [](uint8_t * ptr) {
				reinterpret_cast < _t * > (ptr)->~_t();
			};

			alloc.header.node = table_index;

			auto & obj = _table.get (table_index);
			obj.obj.address = alloc.address;

			_reference_stack.push(table_index);
			new (_paging.template get_ptr < _t > (alloc.address)) _t (std::forward < _args_tv > (args)...);
			_reference_stack.pop();

			return table_index;
		}

		template < typename _t >
		inline _t * get_ptr (table_address obj) {
			return _paging.template get_ptr < _t > (_table.get(obj).obj.address);
		}

		table_address get_root () const {
			if (_reference_stack.empty())
				return 0;

			return _reference_stack.top();
		}

		table_address reg_ref (table_address from, table_address to) {
			return _table.add_ref (from, to);
		}

		void del_ref (table_address from, table_address ref) {
			_table.delete_ref (from, ref);
		}

		inline table_node & get_table_node (table_address address) {
			return _table.get(address);
		}

		void collect () {
			std::vector < table_address > gray_nodes;
			std::vector < table_address > black_nodes;

			black_nodes.push_back (0);

			// mark
			uint32_t index = 0;
			while (!black_nodes.empty()) {
				for (auto const & address : black_nodes) {
					auto & node = _table.get(address);

					node.obj.flags.marked = true;

					// check existing references
					table::enumerator en { node.obj.first_ref };

					if (en) {
						do {
							table_address linked = en.current(&_table).ref.to_object;

							if (_table.get(linked).obj.flags.marked)
								continue;

							gray_nodes.push_back (linked);
						} while (en.next(&_table));
					}
				}

				std::swap (gray_nodes, black_nodes);
				gray_nodes.resize(0);
			}

			// sweep and compress
			_paging.compress(_table);

			// reset markers and clear nodes
			table::enumerator obj_en;

			table_address prev = obj_en.address;
			while (obj_en.next(&_table)) {

				auto & node = obj_en.current(&_table);

				if (node.obj.flags.marked) {
					node.obj.flags.marked = false;
					prev = obj_en.address;
				} else {
					_table.delete_obj(obj_en.address);
					obj_en.address = prev;
				}
			}
		}

	private:
		static thread_local std::stack < uint32_t > _reference_stack;

		paging < page_capacity >	_paging;
		table 						_table;
	};

	template < std::size_t page_capacity >
	thread_local std::stack < uint32_t > collector < page_capacity >::_reference_stack;

}

using namespace memory::literals;

memory::collector < 16_mb > _gc_service;

template < typename _t >
struct gc {
public:

	inline _t * operator -> () { return get(); }

	inline explicit operator bool () const noexcept {
		return _obj == 0;
	}

	inline gc () = default;

	~gc() {
		if (_ref)
			_gc_service.del_ref (_root, _ref);
	}

	template < typename _dt >
	inline explicit gc (
		gc < _dt > const & v,
		std::enable_if_t < std::is_base_of < _t, _dt >::value > = {}
	) {
		copy(v);
	}

	template < typename _dt, typename std::enable_if < std::is_base_of < _t, _dt >::value || std::is_same < _t, _dt >::value >::type = 0 >
	inline gc & operator = (gc < _dt > const & v){
		copy (v);
		return *this;
	}

	inline gc & operator = (gc const & v) {
		copy (v);
		return *this;
	}

	template < typename _dt, typename ... _args_tv >
	friend gc < _dt > gc_new(_args_tv &&...);

private:

	inline explicit gc (memory::table_address address) :
		_obj { address },
		_ref { _gc_service.reg_ref (_root, address ) }
	{}

	inline _t * get () const noexcept {
		if (_ref)
			return _gc_service.get_ptr < _t > (_obj);
		else
			return nullptr;
	}

	template < typename _dt >
	inline void copy (gc < _dt > const & v) {
		if (_ref)
			_gc_service.del_ref (_root, _ref);

		if (v._ref) {
			_obj = v._obj;
			_ref = _gc_service.reg_ref(_root, _obj);
		} else {
			_obj = 0;
			_ref = 0;
		}
	}

	memory::table_address const _root { _gc_service.get_root() };

	memory::table_address 		_ref {0},
								_obj {0};

};

template < typename _t, typename ... _args_tv >
inline gc < _t > gc_new(_args_tv &&... args) {
	return gc < _t > { _gc_service.allocate < _t > (std::forward < _args_tv > (args)...) };
}

struct demo {
	gc < int > cenas;
};

struct demo2 {
	gc < int > cenas;
};

void gc_naive_mark_and_copy (benchmark::State& state) {

	for (auto _ : state)
	{
		{
			auto xx = gc_new<demo>();

			for (std::size_t i = 0; i < state.range(0); ++i) {
				auto yy = gc_new<demo2>();
				xx->cenas = gc_new<int>(321);
				yy->cenas = xx->cenas;
			}
		}

		_gc_service.collect();
	}

}

BENCHMARK(gc_naive_mark_and_copy)->Range(8, 8 << 10);

BENCHMARK_MAIN();