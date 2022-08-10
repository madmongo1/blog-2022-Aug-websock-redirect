//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//

#ifndef BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_WEBSOCK_CONNECTION_HPP
#define BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_WEBSOCK_CONNECTION_HPP

#include "config.hpp"

#include <boost/variant2.hpp>

namespace blog
{

struct websock_connection
{
    using ws_stream  = beast::websocket::stream< tcp::socket >;
    using wss_stream = beast::websocket::stream< ssl::stream< tcp::socket > >;
    using var_type   = boost::variant2::variant< ws_stream, wss_stream >;

    websock_connection(tcp::socket sock)
    : var_(ws_stream(std::move(sock)))
    {
    }

    websock_connection(ssl::stream< tcp::socket > stream)
    : var_(wss_stream(std::move(stream)))
    {
    }

    tcp::socket &
    sock();

    ssl::stream< tcp::socket > *
    query_ssl();

    asio::awaitable< void >
    try_handshake(error_code &ec, beast::websocket::response_type &response, std::string hostname, std::string target);

    asio::awaitable< std::size_t >
    send_text(std::string const &msg);

    asio::awaitable< std::string >
    receive_text();

    asio::awaitable< void >
    close(beast::websocket::close_reason const& reason);

    var_type           var_;
    beast::flat_buffer rxbuffer_;
};

}   // namespace blog

#endif   // BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_WEBSOCK_CONNECTION_HPP
