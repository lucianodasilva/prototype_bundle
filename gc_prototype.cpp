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

	namespace buddy {


		template < typename _t >
		struct paged_deque {
		public:

			paged_deque () {
				add_page();
			}

			inline _t * aquire () {
				auto node = _free_head;

				_free_head = node->next;

				// if no more space in table "grow"
				if (!_free_head)
					add_page();

				*node = {}; // TODO: think about if this is really needed

				return node;
			}

			inline void remove (_t * node) {
				if (node->prev)
					node->prev->next = node->next;

				if (node->next)
					node->next->prev = node->prev;

				node_free(node);
			}

			inline void chain_release (_t * first) {
				while (first) {
					auto *next = first->next;
					node_free(first);
					first = next;
				}
			}

			inline _t * insert_before (_t * node) {
				auto * new_node = aquire ();

				if (node->prev)
					node->prev->next = new_node;

				new_node->prev = node->prev;
				new_node->next = node;

				node->prev = new_node;

				return new_node;
			}

			inline _t * insert_after (_t * node) {
				auto * new_node = aquire ();

				if (node->next)
					node->next->prev = new_node;

				new_node->prev = node->next;
				new_node->prev = node;

				node->next = new_node;

				return new_node;
			}

		private:

			void add_page() {
				_pages.emplace_back(new _t [page_capacity]);

				_free_head = _pages.back().get();
				auto node_count = page_capacity;

				for (std::size_t i = 1; i < node_count; ++i) {
					_free_head[i - 1].next = _free_head + i;
				}

				_free_head[node_count - 1].next = nullptr;
			}

			inline void node_free(_t * node) {
				*node = {};
				node->next = _free_head;
				_free_head = node;
			}

			std::vector<std::unique_ptr< _t []> >
				_pages;
			_t *		_free_head{nullptr};

			static constexpr std::size_t
				page_capacity {4096}; // (for buddy should be memory page size / 32 / 2)
		};

		struct free_block;

		struct header {
			free_block * free_node;
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

		struct free_block {
			free_block * prev;
			free_block * next;

			header * ptr;
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

			paged_deque < free_block > free_nodes;

			free_block * free_levels [4]; // 32 / 64 / 128 / 256 ( for testing should be enough )

			std::unique_ptr < uint8_t [] > buffer { new uint8_t [256] };

			page () {
				// set starting header
				auto max_level = 3U;
				auto * h = header::write (buffer.get(), true, max_level);
				auto * free_node = free_nodes.aquire();

				free_levels [max_level] = free_node;
				free_node->ptr = h;

				h->free_node = free_node;
			}

			static constexpr inline std::size_t level_of (std::size_t length) {
				return std::log2 (length) - std::log2 (32U); // min size
			}

			uint8_t * allocate (std::size_t len) {
				uint8_t block_size = static_cast < uint8_t > (next_pow_2 (len + sizeof (header)));

				auto expected_level = level_of (block_size);
				auto level = expected_level;

				// find smallest fitting free block
				while (!free_levels[level]) {
					++level;
					if (level > 3) throw std::runtime_error ("out of memory");
				}

				auto * h = free_levels[level]->ptr;
				h->free = false;

				// split block until expected length
				while (level > expected_level) {
					// aquire and remove first free node
					auto * node = free_levels [level];

					free_nodes.remove(node);
					free_levels [level] = node->next;

					// split
					--level;

					h->level = level;

					// set node info
					auto * b = h->get_buddy();
					b->free = true;
					b->level = level;

					// create node
					free_block * buddy_node = nullptr;
					if (free_levels[level]) {
						buddy_node = free_nodes.insert_before(free_levels[level]);
					} else {
						buddy_node = free_nodes.aquire();
					}

					free_levels[level] = buddy_node;

					// link
					b->free_node = buddy_node;
					buddy_node->ptr = b;
				}

				return h->begin();
			}

			void free (header * h) {
				// coalescing nodes
				while (h->level < 4 && h->get_buddy()->free) {
					auto free_node = h->get_buddy()->free_node;

					if (free_levels[h->level] == free_node)
						free_levels[h->level] = free_node->next;

					free_nodes.remove (free_node);
					++(h->level);
				}

				h->free = true;
				free_block * free_h = nullptr;
				// create node
				if (free_levels[h->level]) {
					free_h = free_nodes.insert_before(free_levels[h->level]);
				} else {
					free_h = free_nodes.aquire();
				}

				free_levels[h->level] = free_h;

				// link
				h->free_node = free_h;
				free_h->ptr = h;
			}

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
		uint8_t *ptr;
		table_node *first_ref;
		node_flags flags;
	};

	struct table_ref {
		table_node *to_object;
	};

	struct table_node {
		table_node *prev;
		table_node *next;

		union {
			table_object obj;
			table_ref ref;
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
			add_page();
		}

		inline table_node *get_root() { return _node_pages.front().get(); }

		table_node *add_obj_node() {
			auto *node = node_reserve();
			auto *root = get_root();

			if (root->next)
				node_push_front(root->next, node);

			root->next = node;

			return node;
		}

		table_node *add_ref_node(table_node *from_object, table_node *to_object) {
			auto *node = node_reserve();
			auto *first = from_object->obj.first_ref;

			if (first)
				node_push_front(first, node);

			from_object->obj.first_ref = node;
			node->ref.to_object = to_object;

			return node;
		}

		void rem_obj_node(table_node *object) {
			// clear refs
			node_chain_free(object->obj.first_ref);
			node_remove(object);
		}

		void rem_ref_node(table_node *from_object, table_node *ref) {
			if (from_object->obj.first_ref == ref)
				from_object->obj.first_ref = ref->next;

			node_remove(ref);
		}

		struct iterator {

			iterator &operator++() {
				node = node->next;
				return *this;
			}

			bool operator!=(iterator const &it) {
				return node != it.node;
			}

			table_node &operator*() {
				return *node;
			}

			table_node *node;
		};

		struct iterable {

			inline iterator begin() { return {first}; }

			inline iterator end() { return {nullptr}; }

			table_node *first;
		};

		inline iterable objects() { return {get_root()->next}; }

		inline iterable refs(table_node *object) const { return {object->obj.first_ref}; }

	private:

		static constexpr std::size_t table_page_capacity = 0xFFFF;

		void add_page() {
			_node_pages.emplace_back(new table_node[table_page_capacity]);

			_free_head = _node_pages.back().get();
			auto node_count = table_page_capacity;

			// ignore root
			if (_free_head == _node_pages.front().get()) {
				++_free_head;
				--node_count;
			}

			for (std::size_t i = 1; i < node_count; ++i) {
				_free_head[i - 1].next = _free_head + i;
			}

			_free_head[node_count - 1].next = nullptr;
		}

		inline table_node *node_reserve() {
			auto node = _free_head;

			_free_head = node->next;

			// if no more space in table "grow"
			if (!_free_head)
				add_page();

			*node = {}; // TODO: think about if this is really needed

			return node;
		}

		inline void node_free(table_node *node) {
			node->next = _free_head;
			_free_head = node;
		}

		inline void node_chain_free(table_node *first) {
			while (first) {
				auto *next = first->next;
				node_free(first);
				first = next;
			}
		}

		inline void node_remove(table_node *node) {
			if (node->prev)
				node->prev->next = node->next;

			if (node->next)
				node->next->prev = node->prev;

			node_free(node);
		}

		inline void node_push_front(table_node *first, table_node *node) {
			if (first)
				first->prev = node;

			node->next = first;
		}

		std::vector<std::unique_ptr<table_node[]> >
			_node_pages;
		table_node *_free_head{nullptr};
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
			auto *node = _table.add_obj_node();
			auto alloc = _paging.allocate(sizeof(_t));

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
			return _active_root;
		}

		table_node *reg_ref(table_node *from, table_node *to) {
			if (!from)
				from = _table.get_root();

			return _table.add_ref_node(from, to);
		}

		void del_ref(table_node *from, table_node *ref) {
			if (!from)
				from = _table.get_root();

			_table.rem_ref_node(from, ref);
		}

		void collect() {
			std::vector<table_node *> gray_nodes;
			std::vector<table_node *> black_nodes;

			black_nodes.push_back(_table.get_root());

			// mark
			while (!black_nodes.empty()) {
				for (auto *node: black_nodes) {

					node->obj.flags.marked = true;

					for (auto &ref : _table.refs(node)) {
						auto *object = ref.ref.to_object;

						if (object->obj.flags.marked)
							continue;

						gray_nodes.push_back(object);
					}
				}

				std::swap(gray_nodes, black_nodes);
				gray_nodes.resize(0);
			}

			// sweep and compress
			_paging.compress();

			black_nodes.resize(0);

			// reset markers and clear nodes
			for (auto &obj : _table.objects()) {

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

		paging<page_capacity> _paging;
		table _table;
	};

	template<std::size_t page_capacity>
	thread_local table_node *collector<page_capacity>::_active_root{nullptr};

}

using namespace memory::literals;

memory::collector<16_mb> _gc_service;

template<typename _t>
struct gc {
public:

	inline _t *operator->() { return get(); }

	inline explicit operator bool() const noexcept {
		return _obj == nullptr;
	}

	inline gc() = default;

	~gc() {
		if (_ref)
			_gc_service.del_ref(_root, _ref);
	}

	template<typename _dt>
	inline explicit gc(
		gc<_dt> const &v,
		std::enable_if_t<std::is_base_of<_t, _dt>::value> = {}
	) {
		copy(v);
	}

	template<typename _dt, typename std::enable_if<
		std::is_base_of<_t, _dt>::value || std::is_same<_t, _dt>::value>::type = 0>
	inline gc &operator=(gc<_dt> const &v) {
		copy(v);
		return *this;
	}

	inline gc &operator=(gc const &v) {
		copy(v);
		return *this;
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

private:

	inline explicit gc(memory::table_node *node) :
		_obj{node},
		_ref{_gc_service.reg_ref(_root, node)} {}

	inline _t *get() const noexcept {
		if (_ref)
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
	return gc<_t>{_gc_service.allocate<_t>(std::forward<_args_tv>(args)...)};
}

constexpr uint32_t object_size = 4;//1_kb;

struct demo2;

struct demo {
	uint8_t xxx[object_size];
	gc<demo2> obj_pointer;
};

struct demo2 {
	uint8_t xxx[object_size];
	gc<demo> obj_pointer;
};

struct demo3 : public demo2 {

};

void gc_alloc_assign(benchmark::State &state) {

	for (auto _ : state) {
		auto root = gc_new<demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = gc_new<demo2>();
			obj->obj_pointer = root;
		}

		state.PauseTiming();
		_gc_service.collect();
		state.ResumeTiming();
	}
}

BENCHMARK(gc_alloc_assign)->Range(1 << 8, 1 << 16);

void gc_collect(benchmark::State &state) {

	for (auto _ : state) {
		state.PauseTiming();
		auto root = gc_new<demo>();

		for (std::size_t i = 0; i < state.range(0); ++i) {
			auto obj = gc_new<demo2>();
			obj->obj_pointer = root;
		}
		state.ResumeTiming();

		_gc_service.collect();
	}
}

BENCHMARK(gc_collect)->Range(1 << 8, 1 << 16);

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

BENCHMARK_MAIN();
