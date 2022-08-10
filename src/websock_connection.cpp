//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//

#include "websock_connection.hpp"

namespace blog
{
namespace
{
// credit to cppreference
template < class... Ts >
struct overloaded : Ts...
{
    using Ts::operator()...;
};
template < class... Ts >
overloaded(Ts...) -> overloaded< Ts... >;
}   // namespace

asio::awaitable< std::string >
websock_connection::receive_text()
{
    using asio::use_awaitable;

    auto rxsize = co_await visit(
        [&](auto &ws) { return ws.async_read(rxbuffer_, use_awaitable); },
        var_);
    auto is_text = visit([](auto &ws) { return ws.got_text(); }, var_);
    auto result  = beast::buffers_to_string(rxbuffer_.data());
    rxbuffer_.consume(rxsize);
    co_return result;
}

asio::awaitable< std::size_t >
websock_connection::send_text(std::string const &msg)
{
    using asio::use_awaitable;

    return visit(
        [&](auto &ws)
        {
            ws.text();
            return ws.async_write(asio::buffer(msg), use_awaitable);
        },
        var_);
}

asio::awaitable< void >
websock_connection::close(beast::websocket::close_reason const &reason)
{
    using asio::use_awaitable;

    return visit(
        [&](auto &ws) { return ws.async_close(reason, use_awaitable); }, var_);
}

tcp::socket &
websock_connection::sock()
{
    return visit(overloaded { [](ws_stream &ws) -> tcp::socket &
                              { return ws.next_layer(); },
                              [](wss_stream &wss) -> tcp::socket &
                              { return wss.next_layer().next_layer(); } },
                 var_);
}

ssl::stream< tcp::socket > *
websock_connection::query_ssl()
{
    return visit(
        overloaded { [](ws_stream &ws) -> ssl::stream< tcp::socket > *
                     { return nullptr; },
                     [](wss_stream &wss) -> ssl::stream< tcp::socket > *
                     { return &wss.next_layer(); } },
        var_);
}

asio::awaitable< void >
websock_connection::try_handshake(error_code                      &ec,
                                  beast::websocket::response_type &response,
                                  std::string                      hostname,
                                  std::string                      target)
{
    using asio::redirect_error;
    using asio::use_awaitable;

    response.result(beast::http::status::unknown);
    return visit(
        [&](auto &ws)
        {
            return ws.async_handshake(
                response, hostname, target, redirect_error(use_awaitable, ec));
        },
        var_);
}

}   // namespace blog