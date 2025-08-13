#pragma once
#ifndef PROTOTYPE_BUNDLE_BASIC_ALLOC_H
#define PROTOTYPE_BUNDLE_BASIC_ALLOC_H

namespace sgc2 {

    template < typename type >
    concept alloc_source = requires {
        { type::alloc_type };
    };

    template < alloc_source source_t >
    struct basic_alloc {

        using alloc_type = source_t::alloc_type;

        alloc_type alloc () {
            auto * obj = source_t::alloc();

            if (obj) {
                return obj;
            }

            do {
                source_t::grow ();
                obj = source_t::alloc();
            } while (!obj);

            return obj;
        }

        void free (alloc_type obj) {
            bool const is_obj_container_full = source_t::is_container_full(obj);
            source_t::free(obj);
        }

    };

}

#endif //PROTOTYPE_BUNDLE_BASIC_ALLOC_H