//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//

#ifndef BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_URL_HPP
#define BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_URL_HPP

#include <boost/describe.hpp>

#include <ostream>
#include <string>

namespace blog
{

enum class transport_type
{
    tcp,
    tls
};
std::ostream &
operator<<(std::ostream &, transport_type);

BOOST_DESCRIBE_ENUM(transport_type, tcp, tls)

struct url_parts
{
    std::string    hostname;
    std::string    service;
    std::string    path_etc;
    transport_type transport;
};
BOOST_DESCRIBE_STRUCT(url_parts, (), (hostname, service, path_etc, transport))

std::ostream &
operator<<(std::ostream &, const url_parts &);

/// decode a url into component parts that we can use
url_parts
decode_url(std::string const &url);

}   // namespace blog

#endif   // BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_URL_HPP
