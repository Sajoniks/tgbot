#pragma once

#include <boost/noncopyable.hpp>
#include <boost/asio/thread_pool.hpp>

class rest_request : boost::noncopyable
{
public:
    
};

class rest_client : boost::noncopyable {

public:

    explicit rest_client(const std::shared_ptr<boost::asio::execution_context>& ctx);
    ~rest_client() = default;



private:
    std::shared_ptr<boost::asio::execution_context> _ctx;
};