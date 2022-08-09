#include "config.hpp"
#include "fmt_describe.hpp"
#include "server.hpp"
#include "url.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/scope_exit.hpp>
#include <boost/variant2.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>

#include <iostream>

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

    var_type           var_;
    beast::flat_buffer rxbuffer_;
};

// helper type for the visitor #4
template < class... Ts >
struct overloaded : Ts...
{
    using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template < class... Ts >
overloaded(Ts...) -> overloaded< Ts... >;

asio::awaitable< std::string >
websock_connection::receive_text()
{
    using asio::use_awaitable;

    auto rxsize  = co_await visit([&](auto &ws) { return ws.async_read(rxbuffer_, use_awaitable); }, var_);
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

tcp::socket &
websock_connection::sock()
{
    return visit(overloaded { [](ws_stream &ws) -> tcp::socket & { return ws.next_layer(); },
                              [](wss_stream &wss) -> tcp::socket & { return wss.next_layer().next_layer(); } },
                 var_);
}

ssl::stream< tcp::socket > *
websock_connection::query_ssl()
{
    return visit(overloaded { [](ws_stream &ws) -> ssl::stream< tcp::socket > * { return nullptr; },
                              [](wss_stream &wss) -> ssl::stream< tcp::socket > * { return &wss.next_layer(); } },
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
    return visit([&](auto &ws) { return ws.async_handshake(response, hostname, target, redirect_error(use_awaitable, ec)); }, var_);
}

asio::awaitable< std::unique_ptr< websock_connection > >
connect_websock(ssl::context &sslctx, std::string urlstr, int const redirect_limit = 5)
{
    using asio::experimental::deferred;

    auto ex = co_await asio::this_coro::executor;

    int redirects = 0;

again:
    fmt::print("attempting connection: {}\n", urlstr);
    auto decoded = decode_url(urlstr);

    auto result = std::unique_ptr< websock_connection >();
    if (decoded.transport == transport_type::tls)
        result = std::make_unique< websock_connection >(ssl::stream< tcp::socket >(ex, sslctx));
    else
        result = std::make_unique< websock_connection >(tcp::socket(ex));

    auto &sock = result->sock();
    auto *tls  = result->query_ssl();

    auto resolver   = tcp::resolver(ex);
    auto ep_results = co_await resolver.async_resolve(decoded.hostname, decoded.service, deferred);
    co_await asio::async_connect(sock, ep_results, deferred);
    if (tls)
    {
        if (!SSL_set_tlsext_host_name(tls->native_handle(), decoded.hostname.c_str()))
            throw system_error(error_code { static_cast< int >(::ERR_get_error()), asio::error::get_ssl_category() });
        co_await tls->async_handshake(ssl::stream_base::client, deferred);
    }

    auto ec       = error_code();
    auto response = beast::websocket::response_type();
    fmt::print("...handshake\n");
    co_await result->try_handshake(ec, response, decoded.hostname, decoded.path_etc);

    if (ec)
    {
        fmt::print("...error: {}\n{}", ec.message(), boost::lexical_cast< std::string >(response.base()));
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
        fmt::print("...success\n{}", boost::lexical_cast< std::string >(response.base()));
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
    // auto connection = co_await connect_websock(sslctx, "ws://websocket.com/some/part.html?foo=bar#100");
    auto connection = co_await connect_websock(sslctx, initial_url, 6);
    co_await echo(*connection, "Hello, ");
    co_await echo(*connection, "World!\n");
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
    auto initial_url = [ep = svr.tcp_acceptor_.local_endpoint()]
    { return fmt::format("ws://{}:{}/givemeawebsocket/", ep.address().to_string(), ep.port()); }();

    auto stop_sig = asio::cancellation_signal();
    co_spawn(svr.get_executor(),
             svr.run(),
             asio::bind_cancellation_slot(stop_sig.slot(),
                                          [](std::exception_ptr ep)
                                          {
                                              try
                                              {
                                                  if (ep)
                                                      std::rethrow_exception(ep);
                                              }
                                              catch (std::exception &e)
                                              {
                                                  fmt::print("sever exception: {}\n", e.what());
                                              }
                                          }));

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
