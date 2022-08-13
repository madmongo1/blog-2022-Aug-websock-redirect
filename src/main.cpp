#include "config.hpp"
#include "server.hpp"
#include "websock_connection.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/scope_exit.hpp>
#include <boost/url.hpp>
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

bool
infer_tls(boost::urls::scheme scheme)
{
    switch (scheme)
    {
    case boost::urls::scheme::https:
    case boost::urls::scheme::wss:
        return true;
    default:
        return false;
    }
}

unsigned short
infer_port(boost::urls::scheme scheme)
{
    using enum boost::urls::scheme;

    switch (scheme)
    {
    case https:
    case wss:
        return 443;
    case http:
    case ws:
        return 80;
    case ftp:
        return 21;
    case file:
        throw std::runtime_error("file scheme does not have a default port");
    default:
        throw std::logic_error("infer_port - scheme not implemented");
    }
}

boost::urls::scheme
infer_scheme(unsigned short port)
{
    if (port == 443)
        return boost::urls::scheme::https;
    else if (port == 21)
        return boost::urls::scheme::ftp;
    else
        return boost::urls::scheme::http;
}

std::tuple< boost::urls::scheme, unsigned short >
infer_scheme_and_port(boost::urls::url_view u)
{
    if (u.has_port())
    {
        auto port = u.port_number();
        if (u.has_scheme())
        {
            return std::make_tuple(u.scheme_id(), port);
        }
        else
        {
            return std::make_tuple(infer_scheme(port), port);
        }
    }
    else
    {
        if (u.has_scheme())
        {
            auto scheme = u.scheme_id();
            return std::make_tuple(scheme, infer_port(scheme));
        }
        else
        {
            return std::make_tuple(infer_scheme(80), 80);
        }
    }
}

boost::urls::url
resolve_redirect(boost::urls::url_view original, boost::urls::url_view loc)
{
    auto result = boost::urls::url();
    auto ec     = error_code();
    boost::urls::resolve(original, loc, result, ec);
    if (ec)
        throw system_error(ec, "failed to resolve redirect Location");
    return result;
}

/// Return the target form an HTTP URL suitable for specifying as the target in
/// an http request
std::string
target(boost::urls::url u)
{
    u.remove_scheme().remove_authority().remove_userinfo();
    if (!u.is_path_absolute())
        throw std::runtime_error(
            fmt::format("path not absolute: {}", u.string()));
    auto sv = u.string();
    return std::string(sv.begin(), sv.end());
}

asio::awaitable< std::unique_ptr< websock_connection > >
connect_websock(ssl::context &sslctx,
                std::string   urlstr,
                int const     redirect_limit = 5)
{
    using asio::experimental::deferred;
    using namespace std::literals;

    // for convenience, take a copy of the current executor
    auto ex = co_await asio::this_coro::executor;

    // number of redirects detected so far
    int redirects = 0;

    // build a resolver in order tp decode te FQDNs in urls
    auto resolver = tcp::resolver(ex);

    auto url = boost::urls::url(urlstr);

    // in the case of a redirect, we will resume processing here
again:
    fmt::print("attempting connection: {}\n", url.string());

    // decode the URL into components
    auto [scheme, port] = infer_scheme_and_port(url);

    // build the appropriate websocket stream type depending on whether the URL
    // indicates a TCP or TLS transport
    auto result = infer_tls(scheme)
                      ? std::make_unique< websock_connection >(
                            ssl::stream< tcp::socket >(ex, sslctx))
                      : std::make_unique< websock_connection >(tcp::socket(ex));

    // connect the underlying socket of the websocket stream to the first
    // reachable resolved endpoint
    auto host = [&]
    {
        switch (url.host_type())
        {
        case boost::urls::host_type::ipv4:
        case boost::urls::host_type::ipv6:
        case boost::urls::host_type::name:
        {
            auto host = url.encoded_hostname();
            if (host.contains('%'))
                throw std::runtime_error(
                    "We're not dealing with %-encoded hostnames");
            return std::string(host.begin(), host.end());
        }
        case boost::urls::host_type::ipvfuture:
            throw std::runtime_error("Unrecognised host type");
        case boost::urls::host_type::none:
            throw std::runtime_error("No host specified");
        }
        throw std::runtime_error("invalid host type");
    }();
    co_await asio::async_connect(
        result->sock(),
        co_await resolver.async_resolve(host, std::to_string(port), deferred),
        deferred);

    // if the connection is TLS, we will want to update the hostname
    if (auto *tls = result->query_ssl(); tls)
    {
        if (!SSL_set_tlsext_host_name(tls->native_handle(), host.c_str()))
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
        ec, response, url.encoded_hostname(), target(url));

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
                    url = resolve_redirect(
                        url,
                        boost::urls::url_view(
                            response[beast::http::field::location]));
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

    try
    {
        fmt::print(
            "http://www.google.com/foo/bar?baz=1#10 -> {}\n",
            target(boost::urls::url("http://www.google.com/foo/bar?baz=1#10")));
    }
    catch (system_error &se)
    {
        fmt::print("url error: {}\n", se.code().message());
        std::exit(4);
    }
    catch (std::exception &e)
    {
        fmt::print("url error: {}\n", e.what());
    }
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
