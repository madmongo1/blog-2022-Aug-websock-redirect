#include "config.hpp"
#include <fmt/format.h>

namespace blog
{
asio::awaitable<void>
comain()
{
    co_return;
}

}

int
main()
{
    using namespace blog;
    
    using asio::co_spawn;
    using asio::detached;
    
    fmt::print("Initialising\n");

    auto ioc = asio::io_context();
    
    co_spawn(ioc, comain(), detached);
    
    ioc.run();
    fmt::print("Finished\n");
}
