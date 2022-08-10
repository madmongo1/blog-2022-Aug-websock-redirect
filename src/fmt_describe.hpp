//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//

#ifndef BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_FMT_DESCRIBE_HPP
#define BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_FMT_DESCRIBE_HPP

// This code has been copied from the documentation for boost.describe
// All credit to Peter Dimov

#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <fmt/format.h>

#include <type_traits>

template < class T >
struct fmt::formatter<
    T,
    char,
    std::enable_if_t< boost::describe::has_describe_bases< T >::value &&
                      boost::describe::has_describe_members< T >::value &&
                      !std::is_union< T >::value > >
{
    constexpr auto
    parse(format_parse_context &ctx)
    {
        auto it = ctx.begin(), end = ctx.end();

        if (it != end && *it != '}')
        {
            ctx.error_handler().on_error("invalid format");
        }

        return it;
    }

    auto
    format(T const &t, format_context &ctx) const
    {
        using namespace boost::describe;

        using Bd = describe_bases< T, mod_any_access >;
        using Md = describe_members< T, mod_any_access >;

        auto out = ctx.out();

        *out++ = '{';

        bool first = true;

        boost::mp11::mp_for_each< Bd >(
            [&](auto D)
            {
                if (!first)
                {
                    *out++ = ',';
                }

                first = false;

                out = fmt::format_to(
                    out, " {}", (typename decltype(D)::type const &)t);
            });

        boost::mp11::mp_for_each< Md >(
            [&](auto D)
            {
                if (!first)
                {
                    *out++ = ',';
                }

                first = false;

                out = fmt::format_to(out, " .{}={}", D.name, t.*D.pointer);
            });

        if (!first)
        {
            *out++ = ' ';
        }

        *out++ = '}';

        return out;
    }
};

template < class T >
struct fmt::formatter<
    T,
    char,
    std::enable_if_t< boost::describe::has_describe_enumerators< T >::value > >
{
  private:
    using U = std::underlying_type_t< T >;

    fmt::formatter< fmt::string_view, char > sf_;
    fmt::formatter< U, char >                nf_;

  public:
    constexpr auto
    parse(format_parse_context &ctx)
    {
        auto i1 = sf_.parse(ctx);
        auto i2 = nf_.parse(ctx);

        if (i1 != i2)
        {
            ctx.error_handler().on_error("invalid format");
        }

        return i1;
    }

    auto
    format(T const &t, format_context &ctx) const
    {
        char const *s = boost::describe::enum_to_string(t, 0);

        if (s)
        {
            return sf_.format(s, ctx);
        }
        else
        {
            return nf_.format(static_cast< U >(t), ctx);
        }
    }
};

#endif   // BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_FMT_DESCRIBE_HPP
