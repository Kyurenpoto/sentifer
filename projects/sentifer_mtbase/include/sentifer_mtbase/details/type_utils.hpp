#pragma once

#include <tuple>
#include <type_traits>

#include "mtbase_assert.h"

namespace mtbase
{
    template<class Ret, class Func, class TupleArgs, class IndexSeq>
    struct is_tuple_invocable_r_impl
    {};

    template<class Ret, class Func, class TupleArgs, size_t... Index>
    struct is_tuple_invocable_r_impl<Ret, Func, TupleArgs,
        std::index_sequence<Index...>>
    {
        static constexpr bool value =
            std::is_invocable_r_v<Ret, Func,
            std::tuple_element_t<Index, TupleArgs>...>;
    };

    template<class Ret, class Func, class TupleArgs>
    struct is_tuple_invocable_r
    {
        static constexpr bool value =
            is_tuple_invocable_r_impl<Ret, Func, TupleArgs,
            std::make_index_sequence<std::tuple_size_v<
            std::remove_reference_t<TupleArgs>>>>::value;
    };

    template<class Ret, class Func, class TupleArgs>
    inline constexpr bool is_tuple_invocable_r_v =
        is_tuple_invocable_r<Ret, Func, TupleArgs>::value;

    template<class Type, class Tuple>
    struct tuple_extend_front
    {};

    template<class Type, class... Types>
    struct tuple_extend_front<Type, std::tuple<Types...>>
    {
        using type = std::tuple<Type, Types...>;
    };

    template<class Type, class Tuple>
    using tuple_extend_front_t = typename tuple_extend_front<Type, Tuple>::type;

    template<class T>
    struct not_null
    {
        static_assert(std::is_assignable_v<T&, std::nullptr_t>, "T cannot be assigned nullptr.");

        template <typename U>
        constexpr not_null(U&& u) :
            ptr{ std::forward<U>(u) }
        {
            static_assert(std::is_convertible_v<U, T>);

            MTBASE_ASSERT(ptr != nullptr);
        }

        constexpr not_null(T u) :
            ptr{ u }
        {
            static_assert(!std::is_same_v<std::nullptr_t, T>);

            MTBASE_ASSERT(ptr != nullptr);
        }

        template <typename U>
        constexpr not_null(const not_null<U>& other) :
            not_null{ other.get() }
        {
            static_assert(std::is_convertible_v<U, T>);
        }

        not_null(const not_null& other) = default;
        not_null& operator=(const not_null& other) = default;

        not_null(std::nullptr_t) = delete;
        not_null& operator=(std::nullptr_t) = delete;

    public:
        constexpr T get() const
        {
            MTBASE_ASSERT(ptr != nullptr);

            return ptr;
        }

        constexpr operator T() const
        {
            return get();
        }

        constexpr T operator->() const
        {
            return get();
        }

        constexpr decltype(auto) operator*() const
        {
            return *get();
        }

    private:
        T ptr;
    };
}
