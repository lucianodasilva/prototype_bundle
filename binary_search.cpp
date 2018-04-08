#include <iostream>
#include <cinttypes>
#include <type_traits>

namespace tmap {
    template<class ... item_v>
    struct seq {
    };

    template<class item_t>
    struct seq<item_t> {
        using item = item_t;
    };

    namespace details {

        template<class _t>
        struct seq_pop;

        template<class item_t, class ... item_v>
        struct seq_pop<seq<item_t, item_v...> > {
            using type = seq<item_t>;
        };

        template <>
        struct seq_pop < seq <> > {
            using type = seq <>;
        };

        template<class _t>
        struct seq_pop_rest;

        template<class item_t, class ... item_v>
        struct seq_pop_rest<seq<item_t, item_v...> > {
            using type = seq<item_v...>;
        };

        template <>
        struct seq_pop_rest < seq <> > {
            using type = seq <>;
        };

        template<class, class>
        struct seq_cat;

        template<class ... item_v1, class ... item_v2>
        struct seq_cat<seq<item_v1...>, seq<item_v2...> > {
            using type = seq<item_v1..., item_v2...>;
        };

        template<class, template<class, class> class, class>
        struct filter;

        template<class item_t, template<class, class> class pred_t, class first_item_t, class ... item_v>
        struct filter<seq<item_t>, pred_t, seq<first_item_t, item_v...> > {
            using type = std::conditional_t<
                    pred_t<first_item_t, item_t>::value,
                    typename seq_cat<seq<first_item_t>, typename filter<seq<item_t>, pred_t, seq<item_v...> >::type>::type,
                    typename filter<seq<item_t>, pred_t, seq<item_v...> >::type
            >;
        };

        template<class item_t, template<class, class> class pred_t>
        struct filter<item_t, pred_t, seq<> > {
            using type = seq<>;
        };

        template<uint32_t, class, class>
        struct seq_split_left;

        template<uint32_t n, class ... left_v, class top, class ... right_v>
        struct seq_split_left<n, seq<left_v...>, seq<top, right_v...> > {
            using type = std::conditional_t<
                    sizeof...(left_v) < n,
                    typename seq_split_left<n, seq<left_v..., top>, seq<right_v...>>::type,
                    seq<left_v...>
            >;
        };

        template<uint32_t n, class ... left_v>
        struct seq_split_left<n, seq<left_v...>, seq<> > {
            using type =seq<left_v...>;
        };

        template<uint32_t, class>
        struct seq_split_right;

        template<uint32_t n, class top, class ... right_v>
        struct seq_split_right<n, seq<top, right_v...> > {
            using type = std::conditional_t<
                    (sizeof...(right_v) > n),
                    typename seq_split_right<n, seq<right_v...>>::type,
                    seq<right_v...>
            >;
        };

        template<uint32_t n>
        struct seq_split_right<n, seq<> > {
            using type =seq<>;
        };

        template<class seq_t>
        struct seq_split;

        template<class ... item_v>
        struct seq_split<seq<item_v ...> > {

            static uint32_t const constexpr item_c = sizeof...(item_v);
            static uint32_t const constexpr half_c = item_c / 2;

            using first = typename seq_split_left<half_c, seq<>, seq<item_v...>>::type;
            using second = typename seq_split_right<item_c - half_c, seq<item_v...>>::type;
        };

        template<>
        struct seq_split<seq<> > {
            using first = seq<>;
            using second = seq<>;
        };

        template<class seq_t>
        struct map_node;

        template<class ... item_v>
        struct map_node < seq < item_v... > >{

            using split = seq_split<seq < item_v... >>;

            using type = typename seq_pop<typename split::second>::type;

            using left = typename split::first;
            using right = typename seq_pop_rest<typename split::second>::type;

            template < template < class > class compare_t, class _f_t, class key_t>
            inline static bool search (_f_t && find_callback, key_t && key) {
                std::cout << "node (branch): " << type::item::id << std::endl;
                int32_t compare_v = compare_t < type >::compare (key);

                if (compare_v == 0) {
                    find_callback(key);
                    return true;
                } else if (compare_v < 0) {
                    return map_node < left >::template search < compare_t > (
                            std::forward < _f_t > (find_callback),
                            std::forward < key_t > (key));
                } else if (compare_v > 0) {
                    return map_node < right >::template search < compare_t > (
                            std::forward < _f_t > (find_callback),
                            std::forward < key_t > (key));
                }

                return false;
            }


        };

        template<class item>
        struct map_node<seq<item> > {

            using type = seq <item>;

            template < template < class > class compare_t, class _f_t, class key_t>
            inline static bool search (_f_t && find_callback, key_t && key) {
                std::cout << "node (leaf): " << type::item::id << std::endl;

                int32_t compare_v = compare_t < type >::compare (key);
                if (compare_v == 0) {
                    find_callback(key);
                    return true;
                } else {
                    return false;
                }
            }
        };

        template<>
        struct map_node<seq<> > {
            template < template < class > class compare_t, class _f_t, class key_t>
            inline static bool search (_f_t && find_callback, key_t && key) {
                return false;
            }
        };
    }

    template<class seq_t>
    using pop = typename details::seq_pop<seq_t>::type;

    template<class seq_t>
    using pop_rest = typename details::seq_pop_rest<seq_t>::type;

    template<class seq1_t, class seq2_t>
    using cat = typename details::seq_cat<seq1_t, seq2_t>::type;

    template<class seq_id, template<class, class> class predicate_t, class seq_t>
    using filter = typename details::filter<seq_id, predicate_t, seq_t>::type;

    template < template < class, class > class pred_t, class v, class cv >
    struct not_op {
        static bool const constexpr value = !pred_t< v, cv>::value;
    };

    template<class seq_t, template < class, class > class less_pred_t>
    struct seq_sort {

        using top = pop<seq_t>;
        using rest = pop_rest<seq_t>;

        template < class v, class cv>
        using not_less = not_op < less_pred_t, v, cv >;

        using type = cat<
                typename seq_sort<filter<top, less_pred_t, rest>, less_pred_t>::type,
                cat<top, typename seq_sort<filter<top, not_less, rest>, less_pred_t>::type>>;
    };

    template<class item_t, template  <class, class> class less_pred_t>
    struct seq_sort<seq<item_t>, less_pred_t> {
        using type = seq<item_t>;
    };

    template<template < class, class >class less_pred_t>
    struct seq_sort<seq<>, less_pred_t> {
        using type = seq<>;
    };

    template<class seq_t, template <class, class> class less_pred_t>
    using sort = typename seq_sort<seq_t, less_pred_t>::type;

    template<class seq_t>
    using split = details::seq_split<seq_t>;

    template < class seq_t, template < class, class > class less_pred_t >
    using map = details::map_node < sort < seq_t, less_pred_t > >;

}

template<class>
struct printer;

template<class item_t, class ... item_v>
struct printer<tmap::seq<item_t, item_v...> > {
    inline static void print() {
        std::cout << item_t::id << std::endl;
        printer<tmap::seq<item_v...> >::print();
    }
};

template<>
struct printer<tmap::seq<>> {
    inline static void print() {};
};

template<uint32_t id_v>
struct handler {
    static uint32_t const constexpr id = id_v;
};

template<class v, class cv>
struct less {
    static bool const constexpr value = v::id < cv::id;
};

template < class v_t >
struct comparer {
    template < class cv_t >
    static int32_t compare (cv_t && cv) {
        return  cv - v_t::item::id ;
    }
};

int main() {
    using namespace tmap;

    using seq_type = seq<
            handler<3>,
            handler<5>,
            handler<8>,
            handler<2>,
            handler<4>,
            handler<6>,
            handler<1>,
            handler<9>,
            handler<7>,
            handler<10>,
            handler<0>
    >;

    printer<sort<seq_type, less>>::print();
    std::cout << " --- " << std::endl;

    int32_t found = -1;

    bool result = map < seq_type, less >::search <comparer>([&](auto x){
        found = x;
    }, 3);

    std::cout << "found (" << (result ? "true":"false") << "): " << found << std::endl;

    return 0;
}