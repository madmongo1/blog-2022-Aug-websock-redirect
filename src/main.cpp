#include "config.hpp"
#include "server.hpp"
#include "url.hpp"
#include "websock_connection.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/scope_exit.hpp>
#include <fmt/format.h>

#include <iostream>

namespace blog
{

asio::awaitable< std::unique_ptr< websock_connection > >
connect_websock(ssl::context &sslctx,
                std::string   urlstr,
                int const     redirect_limit = 5)
{
    using asio::experimental::deferred;

    auto ex = co_await asio::this_coro::executor;

    int redirects = 0;

again:
    fmt::print("attempting connection: {}\n", urlstr);
    auto decoded = decode_url(urlstr);

    auto result = std::unique_ptr< websock_connection >();
    if (decoded.transport == transport_type::tls)
        result = std::make_unique< websock_connection >(
            ssl::stream< tcp::socket >(ex, sslctx));
    else
        result = std::make_unique< websock_connection >(tcp::socket(ex));

    auto &sock = result->sock();
    auto *tls  = result->query_ssl();

    auto resolver   = tcp::resolver(ex);
    auto ep_results = co_await resolver.async_resolve(
        decoded.hostname, decoded.service, deferred);
    co_await asio::async_connect(sock, ep_results, deferred);
    if (tls)
    {
        if (!SSL_set_tlsext_host_name(tls->native_handle(),
                                      decoded.hostname.c_str()))
            throw system_error(
                error_code { static_cast< int >(::ERR_get_error()),
                             asio::error::get_ssl_category() });
        co_await tls->async_handshake(ssl::stream_base::client, deferred);
    }

    auto ec       = error_code();
    auto response = beast::websocket::response_type();
    fmt::print("...handshake\n");
    co_await result->try_handshake(
        ec, response, decoded.hostname, decoded.path_etc);

    if (ec)
    {
        fmt::print("...error: {}\n{}",
                   ec.message(),
                   boost::lexical_cast< std::string >(response.base()));
        auto http_result = response.result_int();
        switch (response.result())
        {
        case beast::http::status::permanent_redirect:
        case beast::http::status::temporary_redirect:
        case beast::http::status::multiple_choices:
        case beast::http::status::found:
        case beast::http::status::see_other:
        case beast::http::status::moved_permanently:
            if (response.count(beast::http::field::location))
            {
                if (++redirects <= redirect_limit)
                {
                    auto &loc = response[beast::http::field::location];
                    urlstr.assign(loc.begin(), loc.end());
                    goto again;
                }
                else
                {
                    throw std::runtime_error("too many redirects");
                }
            }
            else
            {
                std::stringstream ss;
                ss << "malformed redirect\r\n";
                ss << response;
                throw system_error(ec, ss.str());
            }
            break;

        default:
        {
            std::stringstream ss;
            ss << response;
            throw system_error(ec, ss.str());
        }
        }
    }
    else
    {
        // successful handshake
        fmt::print("...success\n{}",
                   boost::lexical_cast< std::string >(response.base()));
    }

    co_return result;
}

asio::awaitable< void >
echo(websock_connection &conn, std::string const &msg)
{
    co_await conn.send_text(msg);
    fmt::print("{}", co_await conn.receive_text());
}

asio::awaitable< void >
comain(ssl::context &sslctx, std::string initial_url)
{
    fmt::print("enter: {}()\n", __func__);
    // auto connection = co_await connect_websock(sslctx,
    // "ws://websocket.com/some/part.html?foo=bar#100");
    auto connection = co_await connect_websock(sslctx, initial_url, 6);
    co_await echo(*connection, "Hello, ");
    co_await echo(*connection, "World!\n");
    co_await connection->close(beast::websocket::close_reason(
        beast::websocket::close_code::going_away, "thanks for the chat!"));
    co_return;
}

}   // namespace blog

int
main()
{
    using namespace blog;

    using asio::co_spawn;
    using asio::detached;

    fmt::print("Initialising\n");

    auto ioc   = asio::io_context();
    auto ioctx = ssl::context(ssl::context::tls_client);

    auto svr         = server(ioc.get_executor());
    auto initial_url = fmt::format("{}/websocket-4", svr.tcp_root());

    auto stop_sig = asio::cancellation_signal();
    svr.run(stop_sig.slot());

    co_spawn(ioc,
             comain(ioctx, initial_url),
             [&](std::exception_ptr ep)
             {
                 stop_sig.emit(asio::cancellation_type::all);
                 try
                 {
                     if (ep)
                         std::rethrow_exception(ep);
                 }
                 catch (std::exception &e)
                 {
                     fmt::print("client exception: {}\n", e.what());
                 }
             });
    ioc.run();
    fmt::print("Finished\n");
}
