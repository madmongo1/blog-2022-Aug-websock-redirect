//
// Copyright (c) 2022 Richard Hodges (hodges.r@gmail.com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/madmongo1/router
//

#ifndef BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_SERVER_HPP
#define BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_SERVER_HPP

#include "config.hpp"

namespace blog
{

struct server
{
    server(asio::any_io_executor exec);

    asio::awaitable< void >
    run();

    asio::any_io_executor const &
    get_executor() const
    {
        return exec_;
    }

    std::string
    tcp_root() const
    {
        return tcp_root_;
    }

  private:
    asio::any_io_executor exec_;
    ssl::context          sslctx_;
    tcp::acceptor         tcp_acceptor_;
    tcp::acceptor         tls_acceptor_;
    std::string           tcp_root_;
    std::string           tls_root_;
};

}   // namespace blog
#endif   // BLOG_2022_AUG_WEBSOCK_REDIRECT_SRC_SERVER_HPP
