#include <benchmark/benchmark.h>

#include <memory>
#include <stack>
#include <malloc.h>
#include <iostream>
#include <cstring>
#include <cmath>

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
	struct node_pool {
	public:

		explicit node_pool (std::size_t page_capacity)
			: _page_capacity{page_capacity}
		{
			add_page();
		}

		inline _t * reserve () noexcept {
			auto * node = _free_head;
			_free_head = node->next;

			// if no more space in table "grow"
			if (!_free_head)
				add_page();

			*node = {}; // TODO: think about if this is really needed

			return node;
		}

		inline void release (_t * node) noexcept {
			node->next = _free_head;
			_free_head = node;
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
			}

			page [_page_capacity - 1].next = nullptr;

			_free_head = page;
		}

		std::vector < std::unique_ptr < _t [] > >
							_node_pages;
		_t * 				_free_head { nullptr };
		std::size_t const 	_page_capacity;
	};


	template < typename _t >
	struct node_chain {
	public:
		_t * head;

		bool empty () const noexcept {
			return head == nullptr;
		}

		void prepend (_t * node) {
			if (head) {
				head->prev = node;
			}

			node->next = head;
			head = node;
		}

		void remove (_t * node) {
			if (node->next)
				node->next->prev = node->prev;
			if (node->prev)
				node->prev->next = node->next;

			if (node == head) {
				head = node->next;
				node->prev = nullptr;
			}
		}

		void clear (node_pool < _t > & pool) {
			while (head) {
				auto * next = head->next;
				pool.release (head);
				head = next;
			}
		}

		_t * pop () {
			auto * node = head;
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

		iterator begin () { return {head}; }
		iterator end () const { return { nullptr }; }
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

			header * get_buddy () {
				return reinterpret_cast < header * > (
					reinterpret_cast <uintptr_t> (this) ^ (32U << level)
				);
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
				_buffer_size {  32U << (level_of (capacity) + 1U)},
				_top_level { level_of (_buffer_size) - 1 },
				_free_nodes {_buffer_size / 32 / 2 },
				_buffer{ new uint8_t [ _buffer_size ] },
				_free_levels { new memory::node_chain < header_node > [_top_level + 1] }
			{
				// set starting header
				auto * h = header::write (_buffer.get(), true, _top_level);
				auto * node = _free_nodes.reserve();

				_free_levels [_top_level].prepend (node);

				node->link (h);
			}

			static constexpr inline std::size_t level_of (std::size_t length) {
				return std::log2 (length) - std::log2 (32U); // min size
			}

			header * allocate (std::size_t len) {
				// calculate block size
				uint8_t block_size = static_cast < uint8_t > (next_pow_2 (len + sizeof (header)));

				auto expected_level = level_of (block_size);
				auto level = expected_level;

				// find smallest fitting free block
				while (_free_levels[level].empty()) {
					++level;
					if (level > _top_level) throw std::runtime_error ("out of memory");
				}

				auto * free_h = _free_levels[level].pop();
				auto * h = free_h->ptr;

				_free_nodes.release (free_h);

				h->free = false;

				// split block until expected length
				while (level > expected_level) {
					// split
					--level;
					h->level = level;

					// set node info
					auto * b = h->get_buddy();
					b->free = true;
					b->level = level;

					// create node
					auto * b_node = _free_nodes.reserve();
					// add to free list
					_free_levels [level].prepend (b_node);

					// link
					b_node->link(b);
				}

				return h;
			}

			void free (header * h) {
				// coalescing nodes
				while (h->level <= _top_level && h->get_buddy()->free) {
					auto * b_node = reinterpret_cast < header_node * > (h->get_buddy()->user_data);

					_free_levels [h->level].remove (b_node);
					_free_nodes.release (b_node);

					// rise one level
					++(h->level);
				}

				h->free = true;
				auto * node = _free_nodes.reserve ();
				node->link (h);

				_free_levels[h->level].prepend (node);
			}

		private:
			std::size_t const 					_buffer_size;
			std::size_t const					_top_level;

			memory::node_pool < header_node > 	_free_nodes; // (top level length / low level length / 2)
			std::unique_ptr < uint8_t [] > 		_buffer;
			std::unique_ptr < memory::node_chain < header_node > [] >
												_free_levels;
		};

	}

	struct node_flags {
		bool marked : 1;
		bool pinned : 1;
	};

	using page_length  = uint32_t;
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
	};

	struct alignas(8) page_header {
		void (*destructor)(uint8_t *);

		page_header * next;
		page_length length;
		table_node * node;

		inline uint8_t *begin() { return reinterpret_cast < uint8_t * > (this) + sizeof(page_header); }

		inline uint8_t *end() { return begin() + length; }

		inline void dispose() {
			(*destructor)(begin());
		}
	};

	struct table {
	public:

		table() {
			_root = _available_nodes.reserve ();
		}

		inline table_node * get_root() const { return _root; }

		table_node *add_obj_node() {
			auto * node = _available_nodes.reserve ();
			_objects.prepend(node);

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
			// theoretically they should clear themselves
			// object->obj.ref_chain.clear (_available_nodes);
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
			swap(p);
		}

		inline page &operator=(page &&p) noexcept {
			swap(p);
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
			auto * node = _table.add_obj_node();
			auto alloc	= _page.allocate (sizeof (_t));

			// set destructor
			void (*destructor)(uint8_t *) = [](uint8_t *ptr) { reinterpret_cast < _t * >(ptr)->~_t(); };
			alloc->user_data = (void *)destructor;

			node->obj.ptr = alloc->begin();

			auto *prev_root = _active_root;
			_active_root = node;
			new(node->obj.ptr) _t(std::forward<_args_tv>(args)...);
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

			black_nodes.resize(0);

			// reset markers and clear nodes
			for (auto & obj : _table.objects()) {

				if (!obj.obj.flags.marked)
					black_nodes.push_back(&obj);

				obj.obj.flags.marked = false;
			}

			for (auto obj_address : black_nodes) {
				auto * header = reinterpret_cast < buddy::header * > (obj_address->obj.ptr - sizeof (buddy::header));
				reinterpret_cast < void (*)(uint8_t * p) > (header->user_data)(header->begin());

				_page.free(header);
				_table.rem_obj_node(obj_address);
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
		std::swap (_obj, g._obj);
		std::swap (_ref, g._ref);
	}

private:

	inline explicit gc_buddy(memory::table_node *node) :
		_obj{node},
		_ref{_gc_buddy_service.reg_ref(_root, node)} {}

	inline _t *get() const noexcept {
		if (_obj)
			return reinterpret_cast < _t *> (_obj->obj.ptr);
		else
			return nullptr;
	}

	template<typename _dt>
	inline void copy(gc_buddy<_dt> const &v) {
		if (_ref)
			_gc_buddy_service.del_ref(_root, _ref);

		if (v._ref) {
			_obj = v._obj;
			_ref = _gc_buddy_service.reg_ref(_root, _obj);
		} else {
			_obj = nullptr;
			_ref = nullptr;
		}
	}

	memory::table_node *const _root{_gc_buddy_service.get_root()};

	memory::table_node *_ref{nullptr};
	memory::table_node *_obj{nullptr};

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
