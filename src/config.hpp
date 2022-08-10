#ifndef BLOG_CONFIG_HPP
#define BLOG_CONFIG_HPP

#include <boost/asio.hpp>
#include <boost/asio/experimental/deferred.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace blog
{
namespace asio  = ::boost::asio;
namespace beast = ::boost::beast;
namespace ssl   = asio::ssl;
namespace ip    = asio::ip;
using tcp       = ip::tcp;

using ::boost::system::error_code;
using ::boost::system::system_error;
}   // namespace blog

#endif
