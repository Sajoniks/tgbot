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

namespace rest {

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

    explicit Response(std::string_view content);

    Response(const Response&) = default;
    Response(Response&&) = default;
    Response& operator=(const Response&) = default;
    Response& operator=(Response&&) = default;
    ~Response() = default;

    [[nodiscard]] const JDoc* get_json() const;

private:
    std::shared_ptr<JDoc> _doc;
};

/**
 * Rest client
 */
class Client {

    class Impl;

public:

    using Callback = std::function<void(const Response&)>;

    Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client();

    void get_async(const Request& request, Callback cb);
    void post_async(const Request& request, Callback cb);

    Response get(const Request& request);
    Response post(const Request& request);

private:
    UniquePtr<Impl> _impl;
};

}}