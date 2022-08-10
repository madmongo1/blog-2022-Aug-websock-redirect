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

template < class... OStreamables >
std::string
stitch(OStreamables &&...oss)
{
    std::stringstream ss;
    ((ss << oss), ...);
    return ss.str();
}

asio::awaitable< std::unique_ptr< websock_connection > >
connect_websock(ssl::context &sslctx,
                std::string   urlstr,
                int const     redirect_limit = 5)
{
    using asio::experimental::deferred;

    // for convenience, take a copy of the current executor
    auto ex = co_await asio::this_coro::executor;

    // number of redirects detected so far
    int redirects = 0;

    // build a resolver in order tp decode te FQDNs in urls
    auto resolver = tcp::resolver(ex);

    // in the case of a redirect, we will resume processing here
again:
    fmt::print("attempting connection: {}\n", urlstr);

    // decode the URL into components
    auto decoded = decode_url(urlstr);

    // build the appropriate websocket stream type depending on whether the URL
    // indicates a TCP or TLS transport
    auto result = decoded.transport == transport_type::tls
                      ? std::make_unique< websock_connection >(
                            ssl::stream< tcp::socket >(ex, sslctx))
                      : std::make_unique< websock_connection >(tcp::socket(ex));

    // connect the underlying socket of the websocket stream to the first
    // reachable resolved endpoint
    co_await asio::async_connect(
        result->sock(),
        co_await resolver.async_resolve(
            decoded.hostname, decoded.service, deferred),
        deferred);

    // if the connection is TLS, we will want to update the hostname
    if (auto *tls = result->query_ssl(); tls)
    {
        if (!SSL_set_tlsext_host_name(tls->native_handle(),
                                      decoded.hostname.c_str()))
            throw system_error(
                error_code { static_cast< int >(::ERR_get_error()),
                             asio::error::get_ssl_category() });
        co_await tls->async_handshake(ssl::stream_base::client, deferred);
    }

    // some variables to receive the result of the handshake attempt
    auto ec       = error_code();
    auto response = beast::websocket::response_type();

    // attempt a websocket handshake, preserving the response
    fmt::print("...handshake\n");
    co_await result->try_handshake(
        ec, response, decoded.hostname, decoded.path_etc);

    // in case of error, we have three scenarios, detailed below:
    if (ec)
    {
        fmt::print("...error: {}\n{}", ec.message(), stitch(response.base()));
        auto http_result = response.result_int();
        switch (response.result())
        {
        case beast::http::status::permanent_redirect:
        case beast::http::status::temporary_redirect:
        case beast::http::status::multiple_choices:
        case beast::http::status::found:
        case beast::http::status::see_other:
        case beast::http::status::moved_permanently:
            //
            // Scenario 1: We have been redirected
            //
            if (response.count(beast::http::field::location))
            {
                if (++redirects <= redirect_limit)
                {
                    // perform the redirect by updating the URL and jumping to
                    // the goto label above.
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
                //
                // Scenario 2: we have some other HTTP response which is not an
                // upgrade
                //
                throw system_error(ec,
                                   stitch("malformed redirect\r\n", response));
            }
            break;

        default:
            //
            // Scenario 3: Some other transport error
            //
            throw system_error(ec, stitch(response));
        }
    }
    else
    {
        //
        // successful handshake
        //
        fmt::print("...success\n{}", stitch(response.base()));
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
