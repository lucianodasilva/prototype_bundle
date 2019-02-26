#include <benchmark/benchmark.h>

#include <memory>
#include <stack>
#include <malloc.h>
#include <iostream>
#include <cstring>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <csignal>

#define GC_DIAGNOSTICS

#if __linux__
#define debugbreak() std::raise(SIGINT)
#else
#define debugbreak() __debugbreak()
#endif

#ifdef GC_DIAGNOSTICS
#	define check_break(condition) \
	{ bool __check__ = (condition); if (!__check__) debugbreak(); }
#else
#	define check_break(condition)
#endif

namespace memory {

	namespace concept {

		template<typename _to_t, typename _from_t>
		using Assignable = typename std::enable_if < std::is_assignable<_to_t, _from_t>::value >::type;

	}

	namespace literals {
		constexpr std::size_t operator ""_kb(unsigned long long v) {
			return v << 10;
		}

		constexpr std::size_t operator ""_mb(unsigned long long v) {
			return v << 20;
		}
	}

	template < typename _t >
	struct node_chain {
	public:
		_t * head;

		bool empty() const noexcept {
			return head == nullptr;
		}

		void prepend(_t * node) {
#ifdef GC_DIAGNOSTICS
			// check if node already belongs to chain
			for (auto & n : *this) {
				if (&n == node)
					debugbreak();
			}
#endif
			if (head)
				head->prev = node;

			node->next = head;
			node->prev = nullptr;

			head = node;
		}

		void remove(_t * node) {
#ifdef GC_DIAGNOSTICS
			bool found = false;
			// check if node belongs to chain
			for (auto & n : *this) {
				if (&n == node) {
					found = true;
					break;
				}
			}

			if (!found)
				debugbreak();
#endif

			if (node->next)
				node->next->prev = node->prev;
			if (node->prev)
				node->prev->next = node->next;

			if (node == head) {
				head = node->next;
				node->prev = nullptr;
			}
		}

		template < typename _f >
		void clear(_f release_f) {
			while (head) {
				auto * node = head;
				head = node->next;
				release_f(node);
			}
		}

		_t * pop() {
			auto * node = head;

#ifdef GC_DIAGNOSTICS
			// check if pop on empty chain
			if (!node)
				debugbreak();
#endif
			head = node->next;

			return node;
		}

		struct iterator {
			iterator &operator++() {
				node = node->next;
				return *this;
			}

			bool operator!=(iterator const & it) {
				return node != it.node;
			}

			_t & operator*() {
				return *node;
			}

			_t * node;
		};

		iterator begin() { return { head }; }
		iterator end() const { return { nullptr }; }

	};

	template < typename _t >
	struct node_pool {
	public:

		explicit node_pool (std::size_t page_capacity)
			: _page_capacity{page_capacity}
		{
			add_page();
		}

		inline _t * reserve () noexcept {
			// if no more space in table "grow"
			if (_free_chain.empty())
				add_page();

			auto * node = _free_chain.pop();
			*node = {}; // TODO: think about if this is really needed

			return node;
		}

		inline void release (_t * node) noexcept {
			_free_chain.prepend(node);
		}

	private:

		inline void add_page () {
			_node_pages.emplace_back (new _t [_page_capacity]);

			format_top_page();
		}

		inline void format_top_page () {
			auto * page = _node_pages.back().get();

			page->prev = nullptr;

			for (std::size_t i = 1; i < _page_capacity; ++i) {
				page [i - 1].next = page + i;
				page[i].prev = page + (i - 1);
			}

			page [_page_capacity - 1].next = nullptr;

			_free_chain.head = page;
		}

		std::vector < std::unique_ptr < _t [] > >
							_node_pages;
		node_chain < _t >	_free_chain;
		std::size_t const 	_page_capacity;
	};


	struct node_flags {
		bool marked : 1;
		bool pinned : 1;
	};

	using page_length = uint32_t;
	using page_offset = uint32_t;
	using page_id = uint8_t;


	constexpr page_length align_length(page_length length, uint32_t alignment) {
		return ((length + alignment - 1) / alignment) * alignment;
	}

	struct table_node;

	struct table_object {
		node_chain < table_node >
			ref_chain;
		uint8_t *		ptr;
		node_flags 		flags;
	};

	struct table_ref {
		table_node * 	to;
	};

	struct table_node {
		table_node * 	prev;
		table_node * 	next;

		union {
			table_object
				obj;
			table_ref 	ref;
		};

		void print() {
			std::cout << " -- obj (" << (void *)obj.ptr << ") ref (" << (void *)ref.to << ")\n\r";
		}
	};

	struct alignas(8) page_header {
		void(*destructor)(uint8_t *);

		page_header * next;
		page_length length;
		table_node * node;

		inline uint8_t *begin() { return reinterpret_cast <uint8_t *> (this) + sizeof(page_header); }

		inline uint8_t *end() { return begin() + length; }

		inline void dispose() {
			(*destructor)(begin());
		}
	};


	namespace buddy {

		struct header_node;

		struct header {
			void * user_data;
			bool free 				: 1;
			uint8_t level 			: 7;

			constexpr std::size_t size () const {
				return 32U << level; // min size
			}

			uint8_t * begin () {
				return reinterpret_cast < uint8_t * > (this) + sizeof (header);
			}

			uint8_t * end () {
				return begin () + size();
			}

			inline static header * from_ptr(uint8_t * buffer) {
				return reinterpret_cast <header *> (buffer - sizeof(header));
			}

			inline static header * write (uint8_t * buffer, bool free, uint8_t level) {
				return new (buffer) header { nullptr, free, level};
			}
		};

		struct header_node {
			header_node * prev;
			header_node * next;

			header * ptr;

			inline void link (header * p) {
				ptr = p;
				p->user_data = this;
			}

			void node_print() {
				std::cout << " -- ptr: " << (void *)ptr << "\n\r";
			}
		};

		template<class _t>
		constexpr inline _t next_pow_2(_t v) {
			_t bit_ceil = sizeof(_t) * 8;
			_t bit = 1;

			while (bit < bit_ceil) {
				v |= (v >> bit);
				bit = bit << 1;
			}

			return v + 1;
		}

		struct page {
		public:

			page (std::size_t capacity) :
				_buffer_size { 16 << 20 },
				_top_level { level_of (_buffer_size) },
				_available_nodes { 16 << 20 / 32 / 2 },
				_buffer{ new uint8_t [16 << 20] },
				_free_levels { new memory::node_chain < header_node > [_top_level + 1] }
			{
				// clean up stuff 
				for (int i = 0; i <= _top_level; ++i) {
					_free_levels[i].head = nullptr;
				}
				// set starting header
				auto * h = header::write (_buffer.get(), true, _top_level);
				auto * node = _available_nodes.reserve();

				_free_levels [_top_level].prepend (node);

				node->link (h);
			}

			static inline std::size_t level_of (std::size_t length) {
				return std::log2 (length) - std::log2 (32U); // min size
			}

			header * allocate (std::size_t len) {
				// calculate block size
				std::size_t block_size = next_pow_2 (len + sizeof (header));

				auto expected_level = level_of (block_size);
				auto level = expected_level;

				// find smallest fitting free block
				while (_free_levels[level].empty()) {
					++level;
					if (level > _top_level) throw std::runtime_error ("out of memory");
				}

				auto * h_free_node = _free_levels[level].head;
				auto * h_node = h_free_node ->ptr;

				register_as_used(h_node, expected_level);
				
				// split blocks until expected length
				while (level > expected_level) {
					// split node
					// -- demote
					--level;
					
					// set buddy info
					auto * b = find_buddy(h_node, level);
					register_as_free(b, level);
				}

				return h_node;
			}

			void free (header * h, node_chain < table_node > & objects) {
				// coalescing nodes
				uint8_t level = h->level;

				if (level < _top_level) { // top level does not coalesce since its composed by a single node

					auto * buddy = find_buddy(h, level);

					while (buddy->free) {

						// merge block ( clean free node chain )
						register_as_used (buddy, level);

						// -- move to left most header
						if (buddy < h)
							h = buddy;

						// -- promote level
						++level;

						if (level == _top_level)
							break;

						buddy = find_buddy(h, level);
					}
				}

				// release header
				register_as_free (h, level);
			}

		private:

			inline void register_as_used (header * h, uint8_t level) {
				auto * free_node = reinterpret_cast <header_node *> (h->user_data);
				
				_free_levels [h->level].remove(free_node);
				_available_nodes.release(free_node);

				h->free = false;
				h->level = level;
				h->user_data = nullptr;
			}

			inline void register_as_free (header * h, uint8_t level) {
				auto * free_node = _available_nodes.reserve();
				_free_levels[level].prepend(free_node);

				h->free = true;
				h->level = level;
				
				free_node->link(h);
			}

			header * find_buddy (header * h, uint8_t level) {
				check_break(level <= _top_level);

				auto h_offset = reinterpret_cast < uint8_t * >(h) - _buffer.get();
				auto b_offset = h_offset ^ (32U << level);

				check_break(b_offset >= 0);

				header * b = reinterpret_cast <header *>(_buffer.get() + b_offset);

				return b;
			}

			std::size_t const 					_buffer_size;
			std::size_t const					_top_level;

			memory::node_pool < header_node > 	_available_nodes; // (top level length / low level length / 2)
			std::unique_ptr < uint8_t [] > 		_buffer;

			std::unique_ptr < memory::node_chain < header_node > [] >
												_free_levels;
		};

	}

	struct table {
	public:

		table() {
			_root = _available_nodes.reserve ();
		}
		 
		inline table_node * get_root() const { return _root; }

		table_node *add_obj_node() {
			auto * node = _available_nodes.reserve ();
			_objects.prepend(node);

#ifdef GC_DIAGNOSTICS
			for (auto & o: _objects) {
				check_break(&o == node || o.obj.ptr);
				check_break(&o == node || buddy::header::from_ptr(o.obj.ptr)->user_data);
			}
#endif

			return node;
		}

		table_node *add_ref_node(table_node *from_object, table_node *to_object) {
			auto * node = _available_nodes.reserve();

			from_object->obj.ref_chain.prepend(node);
			node->ref.to = to_object;

			return node;
		}

		void rem_obj_node(table_node *object) {
			// -- clear refs --
			// theoretically they should clear themselves but...
			object->obj.ref_chain.clear([&](table_node * n) { _available_nodes.release (n); });
			// remove object
			_objects.remove(object);
			_available_nodes.release(object);
		}

		void rem_ref_node(table_node *from_object, table_node *ref) {
			from_object->obj.ref_chain.remove (ref);
			_available_nodes.release (ref);
		}

		inline node_chain < table_node > & objects() { return _objects; }

	private:
		node_pool < table_node > 	_available_nodes 	{ 4096 };
		node_chain < table_node > 	_objects 			{ nullptr };
		table_node * 				_root 				{ nullptr };
	};

	template<std::size_t capacity>
	struct page {
	public:

		page_header *begin() { return reinterpret_cast < page_header * > (_buffer); }

		page_header *end() { return reinterpret_cast < page_header * > (_offset); }

		page() = default;

		inline page(page &&p) noexcept {
			this->swap(p);
		}

		inline page &operator=(page &&p) noexcept {
			this->swap(p);
			return *this;
		}

		~page() {
			free(_buffer);
		}

		void swap(page &p) noexcept {
			using std::swap;
			swap(_buffer, p._buffer);
			swap(_offset, p._offset);
		}

		void reset() {
			_offset = _buffer;
		}

		bool has_capacity(page_length length) const noexcept {
			return capacity > ((_offset - _buffer) + length);
		}

		bool empty() const noexcept {
			return _offset == _buffer;
		}

		page_header *allocate(page_length length) {
			auto *header = end();

			length = align_length(length, 8);

			_offset += (length + sizeof(page_header));

			header->next = end();
			header->length = length;

			return header;
		}

		page_header *move_allocated(page_header *header) {
			auto alloc_header = allocate(header->length);

			alloc_header->node = header->node;
			alloc_header->destructor = header->destructor;

			// alignment issues
			std::copy(header->begin(), header->end(), alloc_header->begin());

			return alloc_header;
		}

	private:
		uint8_t *_buffer{
			reinterpret_cast < uint8_t * > (malloc(capacity))
		};

		uint8_t *_offset{_buffer};
	};

	template<std::size_t page_size>
	struct paging {

		using page_type = page<page_size>;

		page_type work_page;
		std::vector<page_type> pages;

		paging() {
			// add active page
			pages.emplace_back();
		}

		page_header *allocate(page_length length) {
			if (!pages.back().has_capacity(length)) {
				pages.emplace_back();
			}

			auto alloc = pages.back().allocate(length);

			return alloc;
		}

		void compress() {

			uint32_t recycle_page = 0;
			work_page.reset();

			// by nature, even if completely full
			// the iterating page should always be one step
			// ahead of the work_page
			for (auto &page : pages) {

				if (page.empty())
					continue;

				auto *block_it = page.begin();
				auto *block_end = page.end();

				while (block_it != block_end) {

					// if work_page handles no more blocks
					// swap with discarded page
					if (!work_page.has_capacity(block_it->length)) {
						std::swap(pages[recycle_page], work_page);
						work_page.reset();
						++recycle_page;
					}

					if (block_it->node->obj.flags.marked) {
						// move from one page to another if marked
						auto alloc = work_page.move_allocated(block_it);
						// update new allocated address
						// TODO: perhaps better if updated within the move_allocated function
						block_it->node->obj.ptr = alloc->begin();
					} else {
						// delete object
						block_it->dispose();
					}

					block_it = block_it->next;
				}
			}

			std::swap(pages[recycle_page], work_page);
			//TODO: implement mechanism to recover pages
			pages.resize(recycle_page + 1);
		}
	};

	template<std::size_t page_capacity>
	struct collector {
	public:

		template<typename _t, typename ... _args_tv>
		inline table_node *allocate(_args_tv &&... args) {
			auto * node = _table.add_obj_node();
			auto alloc	= _paging.allocate(sizeof(_t));

			// set destructor
			alloc->destructor = [](uint8_t *ptr) { reinterpret_cast < _t * >(ptr)->~_t(); };
			alloc->node = node;

			node->obj.ptr = alloc->begin();

			auto *prev_root = _active_root;
			_active_root = node;

			new(alloc->begin()) _t(std::forward<_args_tv>(args)...);

			_active_root = prev_root;

			return node;
		}

		table_node *get_root() const {
			return _active_root ? _active_root : (_active_root = _table.get_root());
		}

		table_node * reg_ref(table_node *from, table_node *to) {
			return _table.add_ref_node(from, to);
		}

		void del_ref(table_node *from, table_node *ref) {
				_table.rem_ref_node(from, ref);
		}

		void collect() {

			std::vector<table_node *> gray_nodes;
			std::vector<table_node *> black_nodes;

			black_nodes.push_back(_table.get_root());

			// mark
			while (!black_nodes.empty()) {
				for (auto * node: black_nodes)
				{
					table_object & obj = node->obj;

					obj.flags.marked = true;

					for (auto & ref : obj.ref_chain) {
						auto * to = ref.ref.to;

						if (to->obj.flags.marked)
							continue;

						gray_nodes.push_back(to);
					}
				}

				std::swap(gray_nodes, black_nodes);
				gray_nodes.resize(0);
			}

			// sweep and compress
			_paging.compress();

			black_nodes.resize(0);

			// reset markers and clear nodes
			for (auto & obj : _table.objects()) {

				if (!obj.obj.flags.marked)
					black_nodes.push_back(&obj);

				obj.obj.flags.marked = false;
			}

			for (auto obj_address : black_nodes) {
				_table.rem_obj_node(obj_address);
			}
		}

	private:
		static thread_local table_node *_active_root;

		paging<page_capacity>
				_paging;
		table 	_table;
	};

	template<std::size_t page_capacity>
	thread_local table_node *collector<page_capacity>::_active_root = { nullptr };

	template<std::size_t page_capacity>
	struct collector_buddy {
	public:

		template<typename _t, typename ... _args_tv>
		inline table_node *allocate(_args_tv &&... args) {
			// request allocation table node
			auto * node = _table.add_obj_node();

			// request memory pages for memory
			auto * alloc = _page.allocate (sizeof (_t));

			// set destructor
			void (*destructor)(uint8_t *) = [](uint8_t *ptr) { reinterpret_cast < _t * >(ptr)->~_t(); };
			alloc->user_data = (void *)destructor;

			check_break(alloc->user_data);

			// link allocation table with memory segment
			node->obj.ptr = alloc->begin();

			// root object stack handling
			auto *prev_root = _active_root;
			_active_root = node;

			// request constructor
			new(alloc->begin()) _t(std::forward<_args_tv>(args)...);

			// pop root object stack
			_active_root = prev_root;

			check_break(buddy::header::from_ptr(node->obj.ptr)->user_data);
			return node;
		}

		table_node *get_root() const {
			return _active_root ? _active_root : (_active_root = _table.get_root());
		}

		table_node * reg_ref(table_node *from, table_node *to) {
			return _table.add_ref_node(from, to);
		}

		void del_ref(table_node *from, table_node *ref) {
			_table.rem_ref_node(from, ref);
		}

		void collect() {
			std::vector<table_node *> gray_nodes;
			std::vector<table_node *> black_nodes;

			black_nodes.push_back(_table.get_root());

			// mark
			while (!black_nodes.empty()) {
				for (auto * node: black_nodes)
				{
					table_object & obj = node->obj;

					obj.flags.marked = true;

					for (auto & ref : obj.ref_chain) {
						auto * to = ref.ref.to;

						if (to->obj.flags.marked)
							continue;

						gray_nodes.push_back(to);
					}
				}

				std::swap(gray_nodes, black_nodes);
				gray_nodes.resize(0);
			}

			black_nodes.resize(0);

			// reset markers and clear nodes
			for (auto & obj : _table.objects()) {
				if (!obj.obj.flags.marked)
					black_nodes.push_back(&obj);

				obj.obj.flags.marked = false;
			}

			for (auto obj_address : black_nodes) {
				// get header
				auto * header = buddy::header::from_ptr(obj_address->obj.ptr);
				// call destructor
				check_break(header->user_data);
				reinterpret_cast < void (*)(uint8_t * p) > (header->user_data)(header->begin());

				// remove node from table
				_table.rem_obj_node(obj_address);

#ifdef GC_DIAGNOSTICS
				bool has_invalid_header = false;
				for (auto& o : _table.objects()) {
					auto* h = buddy::header::from_ptr(o.obj.ptr);
					if (!h->user_data)
					{
						has_invalid_header = true;
						break;
					}
				}

				if (has_invalid_header) {
					for (auto& o : _table.objects()) {
						auto* h = buddy::header::from_ptr(o.obj.ptr);
						std::cout 
							<< (void*)& o 
							<< " | prev: " << o.prev 
							<< " | next: " << o.next 
							<< " | level: " << (int)h->level 
							<< " -> " << h->user_data << std::endl;
					}

					debugbreak();
				}
#endif

				// dealocate from page
				_page.free(header, _table.objects());
			}

		}

	private:
		static thread_local table_node *_active_root;

		buddy::page
				_page { page_capacity };
		table 	_table;
	};

	template<std::size_t page_capacity>
	thread_local table_node *collector_buddy<page_capacity>::_active_root = { nullptr };

}

using namespace memory::literals;
memory::collector<8_mb> _gc_service;

memory::collector_buddy<8_mb> _gc_buddy_service;

template<typename _t>
struct gc {
public:

	inline _t *operator->() { return get(); }

	inline explicit operator bool() const noexcept {
		return _obj == nullptr;
	}

	inline gc() = default;

	template<typename _dt, typename = memory::concept::Assignable <_dt, _t> >
	inline gc(
		gc<_dt> const &v
	) {
		copy(v);
	}

	inline gc ( gc const & v) {
		copy (v);
	}

	template<typename _dt,
		typename = memory::concept::Assignable <_dt, _t>
	> inline gc &operator=(gc<_dt> const &v) {
		copy(v);
		return *this;
	}

	inline gc &operator=(gc const &v) {
		copy(v);
		return *this;
	}

	template<typename _dt, typename = memory::concept::Assignable <_dt, _t> >
	inline gc(
		gc<_dt> && v
	) {
		this->swap (v);
	}

	inline gc ( gc && v) {
		this->swap (v);
	}

	template<typename _dt,
		typename = memory::concept::Assignable <_dt, _t>
	> inline gc &operator=(gc<_dt> &&v) {
		this->swap (v);
		return *this;
	}

	inline gc &operator=(gc && v) {
		this->swap (v);
		return *this;
	}

	~gc() {
		if (_ref)
			_gc_service.del_ref(_root, _ref);
	}

	template <
	    typename _cast_t,
	    typename = memory::concept::Assignable <_cast_t, _t>
	>
	inline gc < _cast_t > as () const {
		return gc < _cast_t > { _obj };
	}

	template<typename _dt, typename ... _args_tv>
	friend gc<_dt> gc_new(_args_tv &&...);

	template <typename>
	friend struct gc;

	template < typename _dt >
	void swap (gc < _dt > & g) {
		std::swap (_obj, g._obj);
		std::swap (_ref, g._ref);
	}

private:

	inline explicit gc(memory::table_node *node) :
		_obj{node},
		_ref{_gc_service.reg_ref(_root, node)} {}

	inline _t *get() const noexcept {
		if (_obj)
			return reinterpret_cast < _t *> (_obj->obj.ptr);
		else
			return nullptr;
	}

	template<typename _dt>
	inline void copy(gc<_dt> const &v) {
		if (_ref)
			_gc_service.del_ref(_root, _ref);

		if (v._ref) {
			_obj = v._obj;
			_ref = _gc_service.reg_ref(_root, _obj);
		} else {
			_obj = nullptr;
			_ref = nullptr;
		}
	}

	memory::table_node *const _root{_gc_service.get_root()};

	memory::table_node *_ref{nullptr};
	memory::table_node *_obj{nullptr};

};

template<typename _t, typename ... _args_tv>
inline gc<_t> gc_new(_args_tv &&... args) {
	return gc<_t> {_gc_service.allocate<_t>(std::forward<_args_tv>(args)...)};
}

template<typename _t>
struct gc_buddy {
public:

	inline _t *operator->() { return get(); }

	inline explicit operator bool() const noexcept {
		return _obj == nullptr;
	}

	inline gc_buddy() = default;

	template<typename _dt, typename = memory::concept::Assignable <_dt, _t> >
	inline gc_buddy(
		gc_buddy<_dt> const &v
	) {
		copy(v);
	}

	inline gc_buddy ( gc_buddy const & v) {
		copy (v);
	}

	template<typename _dt,
		typename = memory::concept::Assignable <_dt, _t>
	> inline gc_buddy &operator=(gc_buddy<_dt> const &v) {
		copy(v);
		return *this;
	}

	inline gc_buddy &operator=(gc_buddy const &v) {
		copy(v);
		return *this;
	}

	template<typename _dt, typename = memory::concept::Assignable <_dt, _t> >
	inline gc_buddy(
		gc<_dt> && v
	) {
		this->swap (v);
	}

	inline gc_buddy ( gc_buddy && v) {
		this->swap (v);
	}

	template<typename _dt,
		typename = memory::concept::Assignable <_dt, _t>
	> inline gc_buddy &operator=(gc<_dt> &&v) {
		this->swap (v);
		return *this;
	}

	inline gc_buddy &operator=(gc_buddy && v) {
		this->swap (v);
		return *this;
	}

	~gc_buddy() {
		if (_ref)
			_gc_buddy_service.del_ref(_root, _ref);
	}

	template <
		typename _cast_t,
		typename = memory::concept::Assignable <_cast_t, _t>
	>
	inline gc_buddy < _cast_t > as () const {
		return gc_buddy < _cast_t > { _obj };
	}

	template<typename _dt, typename ... _args_tv>
	friend gc_buddy<_dt> gc_buddy_new(_args_tv &&...);

	template <typename>
	friend struct gc_buddy;

	template < typename _dt >
	void swap (gc_buddy < _dt > & g) {
		// remove current
		if (_ref) {
			_gc_buddy_service.del_ref(_root, _ref);
			_ref = nullptr;
		}

		if (g._ref) {
			_gc_buddy_service.del_ref(g._root, g._ref);
			g._ref = nullptr;
		}

		// swap
		std::swap (_obj, g._obj);

		if (_obj)
			_ref = _gc_buddy_service.reg_ref(_root, _obj);

		if (g._obj)
			g._ref = _gc_buddy_service.reg_ref(g._root, g._obj);
	}

private:

	inline explicit gc_buddy(memory::table_node *node) :
		_obj{node},
		_ref{_gc_buddy_service.reg_ref(_root, node)} 
	{
		check_break(_obj && _obj->obj.ptr);
	}

	inline _t *get() const noexcept {
		if (_obj)
			return reinterpret_cast < _t *> (_obj->obj.ptr);
		else
			return nullptr;
	}

	template<typename _dt>
	inline void copy(gc_buddy<_dt> const &v) {
		if (_ref) {
			_gc_buddy_service.del_ref(_root, _ref);
			_ref = nullptr;
		}

		if (v._ref) {
			_obj = v._obj;
			_ref = _gc_buddy_service.reg_ref(_root, _obj);
		} else {
			_obj = nullptr;
			_ref = nullptr;
		}

		if (_obj && !_obj->obj.ptr) debugbreak();
	}

	memory::table_node *const _root{_gc_buddy_service.get_root()};

	memory::table_node *_obj{nullptr};
	memory::table_node *_ref{nullptr};

};

template<typename _t, typename ... _args_tv>
inline gc_buddy<_t> gc_buddy_new (_args_tv &&... args) {
	return gc_buddy<_t> {_gc_buddy_service.allocate<_t>(std::forward<_args_tv>(args)...)};
}


constexpr uint32_t object_size = 16;//1_kb;

struct demo2;

struct demo {
	uint8_t xxx[object_size];
	gc<demo2> obj_pointer;

	gc<demo> to;
};

struct demo2 {
	uint8_t xxx[object_size];
	gc<demo> obj_pointer;
};

struct demo3 : public demo2 {

};
/*
void gc_alloc_assign(benchmark::State &state) {

	auto root = gc_new<demo>();
	auto node = root;

	for (auto _ : state) {

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc_new < demo > ();
		}

		state.PauseTiming();
		_gc_service.collect();
		state.ResumeTiming();
	}
}

BENCHMARK(gc_alloc_assign)->Range(1 << 8, 1 << 18);

void gc_collect(benchmark::State &state) {

	auto root = gc_new<demo>();
	auto node = root;

	for (auto _ : state) {
		state.PauseTiming();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc_new < demo > ();
		}

		state.ResumeTiming();

		_gc_service.collect();
	}
}

BENCHMARK(gc_collect)->Range(1 << 8, 1 << 18);
*/

struct demo_buddy {
	uint8_t xxx[object_size];
	gc_buddy<demo_buddy> to;
};

void gc_buddy_alloc_assign(benchmark::State &state) {
	auto root = gc_buddy_new<demo_buddy>();
	auto node = root;

	for (auto _ : state) {

		for (std::size_t i = 0; i < state.range(0); ++i) {
			node->to = gc_buddy_new<demo_buddy>();
		}

		state.PauseTiming();
		_gc_buddy_service.collect();
		state.ResumeTiming();
	}
}

BENCHMARK(gc_buddy_alloc_assign)->Range(1 << 8, 1 << 18);

/*
struct no_gc_demo {
	uint8_t xxx[object_size];
	no_gc_demo *cenas;
};

void no_gc_baseline(benchmark::State &state) {

	std::vector<std::unique_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		auto root = new no_gc_demo();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = new no_gc_demo();
			obj->cenas = root;
			root->cenas = obj;

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}

		state.PauseTiming();
		recovery.clear();
		state.ResumeTiming();
	}
}

BENCHMARK(no_gc_baseline)->Range(1 << 8, 1 << 16);

void shared_ptr_alloc_baseline(benchmark::State &state) {

	std::vector<std::shared_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		auto root = std::make_shared<no_gc_demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = std::make_shared<no_gc_demo>();
			obj->cenas = root.get();
			root->cenas = obj.get();

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}

		state.PauseTiming();
		recovery.clear();
		state.ResumeTiming();
	}
}

BENCHMARK(shared_ptr_alloc_baseline)->Range(1 << 8, 1 << 16);

void shared_ptr_collect_baseline(benchmark::State &state) {

	std::vector<std::shared_ptr<no_gc_demo> > recovery;

	for (auto _ : state) {
		state.PauseTiming();
		auto root = std::make_shared<no_gc_demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = std::make_shared<no_gc_demo>();
			obj->cenas = root.get();
			root->cenas = obj.get();

			state.PauseTiming();
			recovery.emplace_back(obj);
			state.ResumeTiming();
		}
		state.ResumeTiming();

		recovery.clear();
	}
}

BENCHMARK(shared_ptr_collect_baseline)->Range(1 << 8, 1 << 16);
*/
BENCHMARK_MAIN();
