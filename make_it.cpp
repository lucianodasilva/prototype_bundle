#include <iostream>
#include <cinttypes>
#include <memory>
#include <vector>
#include <type_traits>

namespace details {

    template<class _t>
    struct copy_ptr {

        inline copy_ptr() {};

        inline copy_ptr(std::unique_ptr<_t> ptr) : _ptr{ptr} {}

        inline copy_ptr(copy_ptr const & origin) : _ptr{(origin._ptr ? origin._ptr->clone() : nullptr)} {}

        inline copy_ptr const &operator=(copy_ptr const &origin) {
            _ptr.reset(origin._ptr ? origin._ptr->clone() : nullptr);
            return *this;
        }

        inline operator bool() const {
            return _ptr.operator bool();
        }

    private:
        std::unique_ptr<_t> _ptr;

        
    };

    template<class _t>
    struct _setter;

    template<class _class_t, class _field_t>
    struct _setter<_field_t _class_t::*> {

    };

    template<class _return_t, class _class_t, class _field_t>
    struct _setter<_return_t (_class_t::*)(_field_t)> {

    };

    struct property_base {
        virtual property_base *clone() const = 0;
    };

    template<class _t>
    struct property : public property_base {
        virtual property_base *clone() const override {
            return new property();
        }
    };

}

template<class _target_t>
struct maker {
    using target_type = _target_t;

protected:

    template < class _address_t >
    inline auto add_property(_address_t field) {
        _properties.push_back (
                new details::property <
                        typename std::result_of <_address_t>::type
                > ()
        );
    }

private:
    std::vector<details::copy_ptr<details::property_base >>
            _properties;
};

#define property(name)

struct test {
public:
    int field_01;
    float field_02;

    void set_method_01(int x) { private_01 = x; }

    float set_method_02(float y) { private_02 = y; return .0; }

private:
    int private_01;
    float private_02;
};

struct test_maker : public maker<test> {
    property(field_01);
    property(field_02);
};

int main() {

    return 0;
}