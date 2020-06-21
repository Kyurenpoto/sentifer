#pragma once

#include <tuple>
#include <type_traits>

namespace mtbase
{
    namespace type_utils
    {
        template<class Ret, class... Args>
        Ret result_of(Ret(*)(Args...));

        template<class Func>
        using result_of_t = decltype(result_of(std::declval<Func>()));

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
    }
}
