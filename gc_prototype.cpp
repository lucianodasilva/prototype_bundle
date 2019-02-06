#include <benchmark/benchmark.h>

#include <deque>
#include <memory>
#include <limits>
#include <stack>
#include <malloc.h>
#include <zconf.h>
#include <iostream>

namespace memory {

	struct node_flags {
		bool marked : 1;
		bool pinned : 1;
	};

	using page_address = uint32_t;
	using page_length  = uint32_t;
	using table_address = uint32_t;


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
		page_address 		next;
		page_length			length;
		table_address		node;
	};

	struct page {
	public:

		page () :
			_capacity (sysconf (_SC_PAGESIZE)),
			_current_address {0}
		{
			_buffer = reinterpret_cast < uint8_t * > (memalign (_capacity, _capacity));
		}

		~page(){
			free(_buffer);
		}

		page_address allocate (page_length length, table_address table_node) {
			auto address = _current_address;
			_current_address += length + sizeof(page_header); // TODO: automatically align perhaps?

			auto & header = get_header (address);

			header.next = _current_address;
			header.length = length;
			header.node = table_node;

			return address;
		}

		inline page_header & get_header (page_address address) {
			return *reinterpret_cast < page_header * >  (_buffer + address);
		}

		inline page_header & get_header (table_node const & node) {
			return get_header (node.obj.address);
		}

		template < typename _t >
		_t * get_ptr (table_node const & node) {
			return reinterpret_cast < _t * > (_buffer + node.obj.address + sizeof(page_header));
		}

	private:

		uintptr_t const _capacity;
		uint8_t * 		_buffer;
		uintptr_t 		_current_address;
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

		void delete_ref (table_address from_object, table_address ref) {
			remove (_data[from_object].obj.first_ref, ref);
		}

		table_node & get (table_address node) {
			return _data [node];
		}

		struct enumerator {
			table_address address {0};

			inline operator bool () const { return address != 0; }

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

			do {
				auto & item = en.current(this);
				if (item.next == address) {
					item.next = _data [address].next;
					break;
				}
			} while (en.next(this));

			release (address);
		}

		inline table_address reserve () {
			auto index = _free_head;

			_free_head = _data[index].next;
			if (_free_head == 0) {
				_free_head = _free_tail;
				grow_table (_data.size () * 2);
			}

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

	struct collector {
	public:

		template < typename _t, typename ... _args_tv >
		inline table_address allocate (_args_tv && ... args) {
			auto table_index = _table.add_obj();
			auto address = _page.allocate(sizeof(_t), table_index);

			auto & obj = _table.get (table_index);
			obj.obj.address = address;

			_reference_stack.push(table_index);
			new (_page.get_ptr < _t > (obj)) _t (std::forward < _args_tv > (args)...);
			_reference_stack.pop();

			return table_index;
		}

		template < typename _t >
		inline _t * get_ptr (table_address obj) {
			return _page.get_ptr < _t > (_table.get(obj));
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

			// sweep
			table::enumerator obj_en;

			while (obj_en.next(&_table)) {

				auto & node = obj_en.current(&_table);

				if (node.obj.flags.marked) {
					node.obj.flags.marked = false;
				} else {
					std::cout << "Delete: " << obj_en.address << std::endl;
				}
			}
		}

	private:
		static thread_local std::stack < uint32_t > _reference_stack;

		page 			_page;
		table 			_table;
	};

	thread_local std::stack < uint32_t > collector::_reference_stack;

}

memory::collector _gc_service;

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

void gc_naive_sweep (benchmark::State& state) {

	auto xx = gc_new<demo>();

	{
		auto yy = gc_new<demo2>();
		xx->cenas = gc_new < int > (321);
		yy->cenas = xx->cenas;
	}

	_gc_service.collect();

	exit (0);
}

BENCHMARK(gc_naive_sweep);

BENCHMARK_MAIN();