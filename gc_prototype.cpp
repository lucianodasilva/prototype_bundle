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


	constexpr page_length align_length (page_length length, uint32_t alignment)
	{
		return ((length + alignment - 1) / alignment) * alignment;
	}

	struct page_address {
		page_offset	offset 	: 24;
		page_id		page 	: 8;
	};

	struct table_object {
		uint8_t *			ptr;
		table_address 		first_ref;
		node_flags			flags;
	};

	struct table_ref {
		table_address 		to_object;
	};

	struct table_node {
		table_address		prev;
		table_address 		next;

		union {
			table_object 	obj;
			table_ref 		ref;
		};
	};

	struct alignas(8) page_header {
		void (*destructor)(uint8_t *);
		page_header * 		next;
		page_length			length;
		table_address		node;

		inline uint8_t * begin () { return reinterpret_cast < uint8_t * > (this) + sizeof (page_header); }
		inline uint8_t * end () { return begin () + length; }

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
			table_grow (4096);
		}

		inline table_node & get (table_address node) {
			return _nodes [node];
		}

		inline table_node & root () { return get(_root_address); }

		table_address add_obj_node () {
			auto index = node_reserve();
			auto first = root().next;

			if (first)
				node_push_front (first, index);

			root().next = index;

			return index;
		}

		table_address add_ref_node (table_address from_object, table_address to_object) {
			auto index = node_reserve();
			auto first = get(from_object).obj.first_ref;

			if (first)
				node_push_front(first, index);

			get(from_object).obj.first_ref = index;
			get(index).ref.to_object = to_object;

			return index;
		}

		void rem_obj_node (table_address object) {
			// clear refs
			node_chain_free (get(object).obj.first_ref);
			node_remove(object);
		}

		void rem_ref_node (table_address from_object, table_address ref) {

			auto & from_node = get(from_object);

			if (from_node.obj.first_ref == ref)
				from_node.obj.first_ref = get(ref).next;

			node_remove(ref);
		}

		struct iterator {

			inline iterator operator ++ () noexcept {
				address = table_instance->get(address).next;
				return { address, table_instance };
			}

			inline bool operator == (iterator const & it) const noexcept {
				return it.address == address;
			}

			inline bool operator != (iterator const & it) const noexcept {
				return !this->operator == (it);
			}

			inline table_address operator * () const {
				return address;
			}

			table_address 	address {0};
			table * 		table_instance {nullptr};
		};

		struct iterable {

			inline iterator begin () { return { first, table_instance }; }
			inline iterator end() { return {}; }

			table_address 	first;
			table * 		table_instance;
		};

		inline iterable objects () { return { root().next, this }; }
		inline iterable refs (table_address object) { return { get(object).obj.first_ref, this }; }

	private:

		void table_grow (std::size_t new_size) {
			if (_nodes.size() > new_size)
				return;

			_free_head = _nodes.size();

			// ignore root
			if (_free_head == 0)
				_free_head = 1;

			_nodes.resize(new_size);

			for (auto i = _free_head + 1; i < new_size; ++i) {
				get(i - 1).next = i;
			}

			get(new_size - 1).next = 0;
		}

		inline table_address node_reserve () {
			auto address = _free_head;

			_free_head = get(address).next;

			// if no more space in table "grow"
			if (!_free_head)
				table_grow(_nodes.size() * 2);

			get(address) = {}; // TODO: think about if this is really needed

			return address;
		}

		inline void node_free (table_address address) {
			get(address).next = _free_head;
			_free_head = address;
		}

		inline void node_chain_free (table_address first) {
			while (first) {
				table_address next = get(first).next;
				node_free (first);
				first = next;
			}
		}

		inline void node_remove (table_address address) {
			auto & node = get(address);

			if (node.prev)
				get(node.prev).next = node.next;

			if (node.next)
				get(node.next).prev = node.prev;
		}

		inline void node_push_front (table_address first, table_address address) {
			if (first)
				get(first).prev = address;

			get(address).next = first;
		}

		std::vector < table_node >	_nodes;
		table_address const 		_root_address {0};
		table_address				_free_head {1};
	};

	template < std::size_t capacity >
	struct page {
	public:

		page_header * begin () 	{ return reinterpret_cast < page_header * > (_buffer); }
		page_header * end () 	{ return reinterpret_cast < page_header * > (_offset); }

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
			_offset = _buffer;
		}

		bool has_capacity (page_length length) const noexcept {
			return capacity > ((_offset - _buffer) + length);
		}

		bool empty () const noexcept {
			return _offset == _buffer;
		}

		page_header * allocate (page_length length) {
			auto * header = end();

			length = align_length(length, 8);

			_offset += (length + sizeof(page_header));

			header->next = end();
			header->length = length;

			return header;
		}

		page_header * move_allocated (page_header * header) {
			auto alloc_header = allocate (header->length);

			alloc_header->node = header->node;
			alloc_header->destructor = header->destructor;

			// alignment issues
			std::copy(header->begin(), header->end(), alloc_header->begin());

			return alloc_header;
		}

	private:
		uint8_t * _buffer {
			reinterpret_cast < uint8_t * > (memalign (sysconf (_SC_PAGESIZE), capacity))
		};

		uint8_t * _offset { _buffer };
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

		page_header * allocate (page_length length) {
			if (!pages.back ().has_capacity (length)) {
				pages.emplace_back();
			}

			auto alloc = pages.back ().allocate (length);

			return alloc;
		}

		void compress (table & table) {

			uint32_t recycled_page = 0;
			work_page.reset();

			for (auto & page : pages) {

				if (page.empty())
					continue;

				auto * block_it = page.begin();
				auto * block_end = page.end();


				while (block_it != block_end) {
					if (!work_page.has_capacity(block_it->length)) {
						std::swap (pages [recycled_page], work_page);
						work_page.reset();
						++recycled_page;
					}

					if (table.get(block_it->node).obj.flags.marked) {
						// move from one page to another if marked
						auto alloc = work_page.move_allocated(block_it);
						// update new allocated address
						table.get(block_it->node).obj.ptr = alloc->begin();
					} else {
						// delete object
						block_it->dispose();
					}

					block_it = block_it->next;
				}

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
			auto table_index = _table.add_obj_node();

			auto alloc = _paging.allocate (sizeof(_t));

			// set destructor
			alloc->destructor = [](uint8_t * ptr) {
				reinterpret_cast < _t * > (ptr)->~_t();
			};

			alloc->node = table_index;

			auto & obj = _table.get (table_index);
			obj.obj.ptr = alloc->begin();

			_reference_stack.push(table_index);
			new (alloc->begin()) _t (std::forward < _args_tv > (args)...);
			_reference_stack.pop();

			return table_index;
		}

		table_address get_root () const {
			if (_reference_stack.empty())
				return 0;

			return _reference_stack.top();
		}

		table_address reg_ref (table_address from, table_address to) {
			return _table.add_ref_node (from, to);
		}

		void del_ref (table_address from, table_address ref) {
			_table.rem_ref_node (from, ref);
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

					for (auto ref : _table.refs(address)) {
						auto to_address = _table.get(ref).ref.to_object;

						if (_table.get(to_address).obj.flags.marked)
							continue;

						gray_nodes.push_back(to_address);
					}
				}

				std::swap (gray_nodes, black_nodes);
				gray_nodes.resize(0);
			}

			// sweep and compress
			_paging.compress(_table);

			black_nodes.resize(0);

			// reset markers and clear nodes
			for (auto obj_address : _table.objects()) {
				auto & obj = _table.get(obj_address);

				if (obj.obj.flags.marked) {
					obj.obj.flags.marked = false;
				} else {
					black_nodes.push_back (obj_address);
				}
			}

			for (auto obj_address : black_nodes) {
				_table.rem_obj_node (obj_address);
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
			return reinterpret_cast < _t *> (_gc_service.get_table_node(_obj).obj.ptr);
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

struct demo2;

struct demo {
	gc < demo2 > obj_pointer;
};

struct demo2 {
	gc < demo > obj_pointer;
};

void gc_collect (benchmark::State& state) {

	for (auto _ : state)
	{
		auto root = gc_new<demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = gc_new < demo2 > ();
			obj->obj_pointer = root;
			root->obj_pointer = obj;
		}

		_gc_service.collect();
	}
}

BENCHMARK(gc_collect)->Range(1 << 8, 1 << 15);

BENCHMARK_MAIN();