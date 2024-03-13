#pragma once

#include <memory>
#include <future>

#include <boost/noncopyable.hpp>
#include <boost/url.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/execution_context.hpp>

#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/string_body.hpp>
#include "tgapi.h"
#include "tgapi/types/api_types_parse.h"
#include "log/types.h"

namespace tg{

    namespace url = boost::urls;
    namespace system = boost::system;
    namespace beast = boost::beast;
    namespace asio = boost::asio;
    namespace http = beast::http;

namespace rest {

class Client;
class Request;
class Response;

/**
 * Rest request
 */
class Request final {
public:

    explicit Request(std::string_view base);

    Request(const Request&) = default;
    Request(Request&&) = default;
    Request& operator=(const Request&) = default;
    Request& operator=(Request&&) = default;
    ~Request() = default;

    [[nodiscard]] url::segments_ref segments();
    [[nodiscard]] url::params_ref params();
    [[nodiscard]] url::url_view get_url() const;
    [[nodiscard]] std::string_view get_content() const;

    /**
     * Set this request json content
     * @param content   Content json value
     */
    void set_json_content(const JValue& content);

    /**
     * Serialize data into this request as json content
     * @tparam T        Content type
     * @param content   Content to serialize as json
     */
    template<typename T>
    void set_json_content(const T& content) {

        JAlloc a;
        set_json_content(parse::do_parse<JValue>(content, a));
    }

private:
    std::string _stringJsonContent;
    boost::url _url;
};

/**
 * Rest response
 */
class Response final {
public:

    explicit Response(const http::response<http::string_body>& body);

    Response(const Response&) = default;
    Response(Response&&) = default;
    Response& operator=(const Response&) = default;
    Response& operator=(Response&&) = default;
    ~Response() = default;

    [[nodiscard]] const JDoc* get_json() const;

private:
    std::shared_ptr<JDoc> _doc;
};

#pragma region Request handler

namespace detail {

class RequestHandlerBase : private boost::noncopyable {
public:
    virtual ~RequestHandlerBase() = default;

protected:
    RequestHandlerBase() = default;
    void notify_completion(Client* client);
};

class RequestHandler : public RequestHandlerBase {
    void on_resolve(const boost::system::error_code& ec, const boost::asio::ip::tcp::resolver::results_type& r);
    void on_connect(const boost::system::error_code& ec, const boost::asio::ip::tcp::endpoint& ep);
    void on_handshake(const boost::system::error_code& ec);
    void on_sent(const boost::system::error_code& ec, size_t bytesSent);

public:

    RequestHandler(Client* client, Request request, http::verb verb, asio::any_io_executor* executor);

    Future<Response> send_async();
    Response send();

private:
    Request _request;
    http::verb _verb;
    asio::any_io_executor* _executor;
    asio::ip::tcp::resolver _resolver;
    asio::ssl::context _sslContext;
    beast::ssl_stream<boost::beast::tcp_stream> _stream;
    beast::flat_buffer _buffer;
    Client* _owner;
    Promise<Response> _promise;
};

}

#pragma endregion // Request handler

/**
 * Rest client
 */
class Client {

    friend class detail::RequestHandlerBase;

    void request_completed(detail::RequestHandlerBase& req);

public:

    explicit Client(asio::any_io_executor& ctx);

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::future<Response> get_async(const Request& request);
    std::future<Response> post_async(const Request& request);

    Response get(const Request& request);
    Response post(const Request& request);

private:
    asio::any_io_executor* _executor;
    mylog::LoggerPtr _logger;
    std::vector<SharedPtr<detail::RequestHandlerBase>> _requests;
};

}}