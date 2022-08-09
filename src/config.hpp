#ifndef BLOG_CONFIG_HPP
#define BLOG_CONFIG_HPP

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/experimental/deferred.hpp>
#include <boost/beast/websocket.hpp>

namespace blog
{
namespace asio = ::boost::asio;
namespace beast = ::boost::beast;
namespace ssl = asio::ssl;
namespace ip = asio::ip;
using tcp = ip::tcp;

}

#endif
