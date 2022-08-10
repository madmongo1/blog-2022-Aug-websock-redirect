//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//
#include "server.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <fmt/format.h>

#include <regex>

namespace blog
{

namespace
{
std::string
as_text(tcp::endpoint ep)
{
    return fmt::format("{}:{}", ep.address().to_string(), ep.port());
}
}   // namespace

server::server(asio::any_io_executor exec)
: exec_(exec)
, sslctx_(ssl::context_base::sslv23)
, tcp_acceptor_(exec_, tcp::endpoint(ip::address_v4::loopback(), 0))
, tls_acceptor_(exec_, tcp::endpoint(ip::address_v4::loopback(), 0))
, tcp_root_(fmt::format("ws://{}", as_text(tcp_acceptor_.local_endpoint())))
, tls_root_(fmt::format("wss://{}", as_text(tls_acceptor_.local_endpoint())))
{
    sslctx_.set_options(boost::asio::ssl::context::default_workarounds |
                        boost::asio::ssl::context::no_sslv2 |
                        boost::asio::ssl::context::single_dh_use);
    sslctx_.set_password_callback(
        [](std::size_t &, ssl::context_base::password_purpose &) -> std::string
        { return "test"; });
    sslctx_.use_certificate_chain_file("server.pem");
    sslctx_.use_private_key_file("server.pem", boost::asio::ssl::context::pem);
    sslctx_.use_tmp_dh_file("dh4096.pem");
}

namespace
{
asio::awaitable< void >
send_and_die(ssl::stream< tcp::socket >                              &stream,
             beast::http::response< beast::http::string_body > const &response)
{
    using asio::redirect_error;
    using asio::use_awaitable;
    using asio::experimental::deferred;

    auto ec = error_code();
    co_await beast::http::async_write(
        stream, response, asio::redirect_error(use_awaitable, ec));
    if (!ec)
        co_await stream.async_shutdown(asio::redirect_error(use_awaitable, ec));
    auto &sock = stream.next_layer();
    sock.shutdown(asio::socket_base::shutdown_both, ec);
    sock.close();
}

asio::awaitable< void >
send_and_die(tcp::socket                                             &sock,
             beast::http::response< beast::http::string_body > const &response)
{
    using asio::redirect_error;
    using asio::use_awaitable;
    using asio::experimental::deferred;

    auto ec = error_code();
    co_await beast::http::async_write(
        sock, response, asio::redirect_error(use_awaitable, ec));
    sock.shutdown(asio::socket_base::shutdown_both, ec);
    sock.close();
}

template < class Stream >
asio::awaitable< void >
send_redirect(Stream &stream, std::string loc)
{
    using asio::redirect_error;
    using asio::use_awaitable;
    using asio::experimental::deferred;

    auto response = beast::http::response< beast::http::string_body >();
    response.result(beast::http::status::moved_permanently);
    response.set(beast::http::field::location, loc);
    response.keep_alive(false);
    response.body() = fmt::format("please redirect to {}\r\n", loc);
    response.prepare_payload();

    co_await send_and_die(stream, response);
}

template < class Stream >
asio::awaitable< void >
send_error(Stream &stream, beast::http::status stat, std::string message)
{
    using asio::redirect_error;
    using asio::use_awaitable;
    using asio::experimental::deferred;

    auto response = beast::http::response< beast::http::string_body >();
    response.result(stat);
    response.keep_alive(false);
    response.body() = std::move(message);
    response.prepare_payload();
    co_await send_and_die(stream, response);
}

asio::awaitable< void >
serve_http(tcp::socket sock, std::string https_endpoint)
{
    using asio::experimental::deferred;

    auto rxbuf  = beast::flat_buffer();
    auto parser = beast::http::request_parser< beast::http::empty_body >();
    co_await beast::http::async_read(sock, rxbuf, parser, deferred);

    static const auto re      = std::regex("(/websocket-\\d+)(/.*)?",
                                      std::regex_constants::icase |
                                          std::regex_constants::optimize);
    auto              match   = std::cmatch();
    auto             &request = parser.get();
    if (std::regex_match(
            request.target().begin(), request.target().end(), match, re))
    {
        co_await send_redirect(
            sock, fmt::format("{}{}", https_endpoint, match[0].str()));
    }
    else
    {
        co_await send_error(
            sock,
            beast::http::status::not_found,
            fmt::format("resource {} is not recognised\r\n",
                        std::string_view(request.target().data(),
                                         request.target().size())));
    }
}

asio::awaitable< void >
http_server(tcp::acceptor &acceptor, std::string https_endpoint)
{
    using asio::detached;
    using asio::experimental::deferred;
    auto exec = co_await asio::this_coro::executor;

    try
    {
        while (1)
        {
            tcp::socket sock(exec);
            co_await acceptor.async_accept(sock, deferred);
            co_spawn(
                exec, serve_http(std::move(sock), https_endpoint), detached);
        }
    }
    catch (system_error &se)
    {
        fmt::print("http_server: {}\n", se.code().message());
    }
    catch (std::exception &e)
    {
        fmt::print("http_server: {}\n", e.what());
    }
}

asio::awaitable< void >
run_echo_server(beast::websocket::stream< ssl::stream< tcp::socket > > &wss,
                beast::flat_buffer                                     &rxbuf)
{
    using asio::experimental::deferred;

    for (;;)
    {
        auto size = co_await wss.async_read(rxbuf, deferred);
        auto data = rxbuf.cdata();
        co_await wss.async_write(data, deferred);
        rxbuf.consume(size);
    }
}

asio::awaitable< void >
serve_https(ssl::stream< tcp::socket > stream, std::string https_fqdn)
{
    try
    {
        using asio::experimental::deferred;

        co_await stream.async_handshake(ssl::stream_base::server, deferred);

        auto rxbuf   = beast::flat_buffer();
        auto request = beast::http::request< beast::http::string_body >();
        co_await beast::http::async_read(stream, rxbuf, request, deferred);

        auto &sock = stream.next_layer();
        if (beast::websocket::is_upgrade(request))
        {
            static const auto re = std::regex(
                "/websocket-(\\d+)(/.*)?",
                std::regex_constants::icase | std::regex_constants::optimize);
            auto match = std::cmatch();
            if (std::regex_match(request.target().begin(),
                                 request.target().end(),
                                 match,
                                 re))
            {
                auto index = ::atoi(match[1].str().c_str());
                if (index == 0)
                {
                    auto wss =
                        beast::websocket::stream< ssl::stream< tcp::socket > >(
                            std::move(stream));
                    co_await wss.async_accept(request, deferred);
                    co_await run_echo_server(wss, rxbuf);
                    // serve the websocket
                }
                else
                {
                    // redirect to the next index down
                    auto loc = fmt::format("{}/websocket-{}{}",
                                           https_fqdn,
                                           index - 1,
                                           match[2].str());
                    co_await send_redirect(stream, loc);
                }
            }
            else
            {
                co_await send_error(stream,
                                    beast::http::status::not_found,
                                    "try /websocket-5\r\n");
            }
        }
        else
        {
            co_await send_error(
                stream,
                beast::http::status::not_acceptable,
                "This server only accepts websocket requests\r\n");
        }
    }
    catch (system_error &e)
    {
        fmt::print("serve_https: {}\n", e.code().message());
    }
    catch (std::exception &e)
    {
        fmt::print("serve_https: {}\n", e.what());
    }
}

asio::awaitable< void >
wss_server(ssl::context  &sslctx,
           tcp::acceptor &acceptor,
           std::string    https_fqdn)
{
    using asio::detached;
    using asio::experimental::deferred;
    auto exec = co_await asio::this_coro::executor;

    try
    {
        while (1)
        {
            auto sock = tcp::socket(exec);
            co_await acceptor.async_accept(sock, deferred);
            co_spawn(
                exec,
                serve_https(ssl::stream< tcp::socket >(std::move(sock), sslctx),
                            https_fqdn),
                detached);
        }
    }
    catch (system_error &se)
    {
        fmt::print("wss_server: {}\n", se.code().message());
    }
    catch (std::exception &e)
    {
        fmt::print("wss_server: {}\n", e.what());
    }
}

void
print_exceptions(system_error &se);

void
print_exceptions(std::exception &e)
{
    fmt::print("server: {}\n", e.what());
    try
    {
        std::rethrow_if_nested(e);
    }
    catch (system_error &nested)
    {
        print_exceptions(nested);
    }
    catch (std::exception &nested)
    {
        print_exceptions(nested);
    }
}

void
print_exceptions(system_error &se)
{
    fmt::print("server: {}\n", se.code().message());
    try
    {
        std::rethrow_if_nested(se);
    }
    catch (system_error &nested)
    {
        print_exceptions(nested);
    }
    catch (std::exception &nested)
    {
        print_exceptions(nested);
    }
}

void
print_exceptions(std::exception_ptr const &ep)
{
    try
    {
        if (ep)
            std::rethrow_exception(ep);
    }
    catch (system_error &nested)
    {
        print_exceptions(nested);
    }
    catch (std::exception &nested)
    {
        print_exceptions(nested);
    }
}

}   // namespace

void
server::run(asio::cancellation_slot stop_slot)
{
    using namespace asio::experimental::awaitable_operators;
    using asio::bind_cancellation_slot;
    using asio::co_spawn;
    using asio::use_awaitable;

    fmt::print("server starting\n");

    auto handler = [](std::exception_ptr ep)
    {
        try
        {
            if (ep)
                std::rethrow_exception(ep);
        }
        catch (asio::multiple_exceptions &es)
        {
            print_exceptions(es.first_exception());
        }
        catch (std::exception &e)
        {
            print_exceptions(e);
        }
    };

    co_spawn(get_executor(),
             http_server(tcp_acceptor_, tls_root_) &&
                 wss_server(sslctx_, tls_acceptor_, tls_root_),
             bind_cancellation_slot(stop_slot, handler));
}

}   // namespace blog