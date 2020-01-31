#ifndef BLOOMBERGLP_QUANTUM_MAYBE_H
#define BLOOMBERGLP_QUANTUM_MAYBE_H

#include <type_traits>
#include <exception>
#include <tuple>

namespace Bloomberg {
namespace quantum {

template <typename T>
class Maybe
{
public:
    using Type = T;
    
    //Constructors
    Maybe() = default;
    template <typename ...ARGS>
    Maybe(std::piecewise_construct_t, ARGS&&...args) {
        new(&_storage)T(std::forward<ARGS>(args)...);
        _isSet = true;
    }
    template <typename U,
              typename = std::enable_if_t<std::is_copy_constructible<U>::value &&
                                          std::is_same<U,T>::value>>
    Maybe(const U& u) {
        new(&_storage)T(u);
        _isSet = true;
    }
    template <typename U,
              typename = std::enable_if_t<std::is_move_constructible<U>::value &&
                                          std::is_same<U,T>::value>>
    Maybe(U&& u) {
        new(&_storage)T(std::move(u));
        _isSet = true;
    }
    template <typename U,
             typename = std::enable_if_t<std::is_copy_constructible<U>::value>>
    Maybe(const Maybe<U>& other) {
        if (other._isSet) {
            new(&_storage)T(other.value());
            _isSet = true;
        }
    }
    template <typename U,
             typename = std::enable_if_t<std::is_move_constructible<U>::value>>
    Maybe(Maybe<U>&& other) {
        if (other._isSet) {
            new(&_storage)T(std::move(other).value());
            _isSet = true;
            other.reset();
        }
    }
    //Assignment operators
    template <typename U,
              typename = std::enable_if_t<std::is_copy_assignable<U>::value &&
                                          std::is_same<U,T>::value>>
    Maybe& operator=(const U& u) {
        if (_isSet) {
            if (&value() == reinterpret_cast<T*>(&u)) return *this;
            value() = u;
        }
        else {
            new(&_storage)T(u);
            _isSet = true;
        }
    }
    template <typename U,
              typename = std::enable_if_t<std::is_move_assignable<U>::value &&
                                          std::is_same<U,T>::value>>
    Maybe& operator=(U&& u) {
        if (_isSet) {
            if (&value() == reinterpret_cast<T*>(&u)) return *this;
            value() = std::move(u);
        }
        else {
            new(&_storage)T(std::move(u));
            _isSet = true;
        }
        return *this;
    }
    template <typename U,
              typename = std::enable_if_t<std::is_copy_assignable<U>::value>>
    Maybe& operator=(const Maybe<U>& other) {
        if (this == &other) return *this;
        if (other._isSet) {
            *this = other.value();
        }
        else {
            reset();
        }
        return *this;
    }
    template <typename U,
              typename = std::enable_if_t<std::is_move_assignable<U>::value>>
    Maybe& operator=(Maybe<U>&& other) {
        if (this == &other) return *this;
        if (other._isSet) {
            *this = std::move(other).value();
            other.reset();
        }
        else {
            reset();
        }
        return *this;
    }
    //Destructor
    ~Maybe() {
        reset();
    }
    //Accessors
    const T& value() const & {
        if (!_isSet) throw std::runtime_error("Value not set");
        return *reinterpret_cast<const T*>(&_storage);
    }
    T& value() & {
        if (!_isSet) throw std::runtime_error("Value not set");
        return *reinterpret_cast<T*>(&_storage);
    }
    T&& value() && {
        if (!_isSet) throw std::runtime_error("Value not set");
        return std::move(*reinterpret_cast<T*>(&_storage));
    }
    explicit operator bool() const { return _isSet; }
    void reset() {
        if (_isSet) {
            value().~T();
            _isSet = false;
        }
    }
private:
    typename std::aligned_storage<sizeof(T), alignof(T)>::type _storage;
    bool _isSet{false};
};

template <typename T, typename ...ARGS>
Maybe<T> makeMaybe(ARGS&&...args) {
    return Maybe<T>{std::piecewise_construct, std::forward<ARGS>(args)...};
}

}
}

#endif //BLOOMBERGLP_QUANTUM_MAYBE_H
