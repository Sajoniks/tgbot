#include "tgapi/rest_client.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>
#include "boost/asio/thread_pool.hpp"

#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <utility>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "log/logging.h"

namespace tg::rest {

Request::Request(std::string_view base) {
    _url.set_host(base);
    _url.set_scheme_id(boost::urls::scheme::https);
}

boost::url_view Request::get_url() const {
    return _url;
}

boost::urls::segments_ref Request::segments() {
    return _url.segments();
}

boost::urls::params_ref Request::params() {
    return _url.params();
}

std::string_view Request::get_content() const {
    return _stringJsonContent;
}

void Request::set_json_content(const JValue& content) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer writer{ buf };
    content.Accept(writer);
    _stringJsonContent = buf.GetString();
}

Response::Response(std::string_view content) {
    _doc = std::make_shared<rapidjson::Document>();

    rapidjson::StringStream ss{ content.data() };
    _doc->ParseStream(ss);
}

const JDoc* Response::get_json() const {
    return _doc.get();
}

#pragma region Client Implementation

namespace {

namespace system = boost::system;
namespace beast = boost::beast;
namespace asio = boost::asio;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

auto generate_id() {
    static long id;
    ++id;
    return id;
}


// @todo Handle destruction of the RestClient

class RequestHandler {

    void on_resolve(const system::error_code& ec, const tcp::resolver::results_type& r);
    void on_connect(const system::error_code& ec, const tcp::endpoint& ep);
    void on_handshake(const system::error_code& ec);
    void on_sent(const system::error_code& ec, size_t bytesSent);

public:

    using Callback = std::function<void(long, Response)>;

    RequestHandler(Request request, http::verb verb, asio::any_io_executor executor);
    RequestHandler(const RequestHandler&) = delete;
    RequestHandler(RequestHandler&&) = default;
    RequestHandler& operator=(const RequestHandler&) = delete;
    RequestHandler& operator=(RequestHandler&&) = default;

    void send_async(Callback cb);
    Response send();

    long get_id() const;

    friend bool operator==(const RequestHandler& A, long B) {
        return A._id == B;
    }

    ~RequestHandler();

private:
    Request _request;
    http::verb _verb;
    long _id;
    UniquePtr<Callback> _cb;
    asio::any_io_executor _executor;
    asio::ip::tcp::resolver _resolver;
    asio::ssl::context _sslContext;
    beast::ssl_stream<boost::beast::tcp_stream> _stream;
    beast::flat_buffer _buffer;
};

long RequestHandler::get_id() const {
    return _id;
}

RequestHandler::RequestHandler(Request request, http::verb verb, asio::any_io_executor executor)
    : _request{ std::move(request) }
    , _verb{ verb }
    , _id{ generate_id() }
    , _executor{ std::move(executor) }
    , _resolver{ _executor }
    , _sslContext{ ssl::context::tlsv12_client }
    , _stream{ _executor, _sslContext }
{
    _sslContext.set_default_verify_paths();
    _sslContext.set_verify_mode(ssl::verify_peer);
    _sslContext.set_verify_callback(ssl::host_name_verification(_request.get_url().host()));
}

void RequestHandler::send_async(Callback cb) {
    using namespace std::placeholders;

    _cb = make_unique<Callback>(std::move(cb));

    auto resolve = [this](const system::error_code& ec, const tcp::resolver::results_type& r) {
        on_resolve(ec, r);
    };

    if (!SSL_set_tlsext_host_name(_stream.native_handle(), _request.get_url().host().c_str())) {
        std::invoke(*_cb, get_id(), Response{""});
    }
    else {
        _resolver.async_resolve(_request.get_url().host(), _request.get_url().scheme(), resolve);
    }
}

void RequestHandler::on_resolve(const system::error_code& ec, const tcp::resolver::results_type& r) {
    using namespace std::placeholders;

    auto connect = [this](const system::error_code& ec, const tcp::endpoint& r) {
        on_connect(ec, r);
    };

    if (ec) {
        std::invoke(*_cb, get_id(), Response{""});
    } else {
        beast::get_lowest_layer(_stream).async_connect(r, connect);
    }
}

void RequestHandler::on_connect(const system::error_code& ec, const tcp::endpoint& ep) {
    using namespace std::placeholders;

    auto handshake = [this](const system::error_code& ec) {
        on_handshake(ec);
    };

    if (ec){
        std::invoke(*_cb, get_id(), Response{""});
    } else {
        _stream.async_handshake(ssl::stream_base::client, handshake);
    }
}

void RequestHandler::on_handshake(const system::error_code& ec) {
    using namespace std::placeholders;

    if (ec) {
        std::invoke(*_cb, get_id(), Response{""});
    } else {
        http::request<http::string_body> req{ _verb, _request.get_url().encoded_target(), 11 };
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(http::field::host, _request.get_url().host());
        if (!_request.get_content().empty()) {
            req.set(http::field::content_type, "application/json");
            req.body() = _request.get_content();
            req.prepare_payload();
        }

        system::error_code writeEc;
        const auto written = http::write(_stream, req, writeEc);

        on_sent(writeEc, written);
    }
}

void RequestHandler::on_sent(const system::error_code& ec, const size_t) {

    if (ec) {
        std::invoke(*_cb, get_id(), Response{""});
    } else {

        http::response<http::string_body> res;
        http::read(_stream, _buffer, res);

        Response r{ res.body() };
        std::invoke(*_cb, get_id(), r);
    }
}

Response RequestHandler::send() {

    if (!SSL_set_tlsext_host_name(_stream.native_handle(), _request.get_url().host().c_str()))
    {
        throw std::runtime_error("ssl Error");
    }

    auto results = _resolver.resolve(_request.get_url().host(), _request.get_url().scheme());
    beast::get_lowest_layer(_stream).connect(results);
    _stream.handshake(ssl::stream_base::client);

    http::request<http::string_body> req{ _verb, _request.get_url().encoded_target(), 11 };
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::host, _request.get_url().host());
    if (!_request.get_content().empty())
    {
        req.set(http::field::content_type, "application/json");
        req.body() = _request.get_content();
        req.prepare_payload();
    }
    http::write(_stream, req);

    http::response<http::string_body> res;
    http::read(_stream, _buffer, res);

    return Response{ res.body() };
}

tg::rest::RequestHandler::~RequestHandler() = default;

}

class Client::Impl {
    void received_response(long id, Response response, Client::Callback callback);
public:

    Impl();

    Impl(const Impl&) = delete;
    Impl(Impl&&) = delete;

    Impl& operator=(const Impl&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() = default;

    void get_async(const Request& request, Client::Callback getCallback);
    void post_async(const Request& request, Client::Callback postCallback);

    Response get(const Request& request);
    Response post(const Request& request);

private:
    UniquePtr<asio::thread_pool> _tp;
    mylog::LoggerPtr _logger;
    std::mutex _mutex;
    std::vector<RequestHandler> _requests;
};

Client::Impl::Impl() {
    _logger = mylog::LogManager::get().create_logger("Rest");
    _tp = make_unique<asio::thread_pool>(4);
}

void Client::Impl::received_response(long id, tg::rest::Response response, Client::Callback cb) {
    {
        auto lock = std::unique_lock(_mutex);
        _requests.erase(std::remove(_requests.begin(), _requests.end(), id), _requests.end());
    }
    cb(response);
}

void Client::Impl::get_async(const Request& request, Client::Callback getCallback) {

    const auto callbackFunction = [this, cb = std::move(getCallback)](long id, Response r) {
       received_response(id, std::move(r), cb);
    };

    auto lock = std::unique_lock(_mutex);
    _logger->info("GET: {}", request.get_url().data());
    RequestHandler& handler = _requests.emplace_back(request, http::verb::get, _tp->get_executor());
    handler.send_async(std::move(callbackFunction));
}

void Client::Impl::post_async(const Request& request, Client::Callback postCallback) {

    const auto callbackFunction = [this, cb = std::move(postCallback)](long id, Response r) {
        received_response(id, std::move(r), cb);
    };

    auto lock = std::unique_lock(_mutex);
    _logger->info("POST: {}", request.get_url().data());
    RequestHandler& handler = _requests.emplace_back(request, http::verb::post, _tp->get_executor());
    handler.send_async(std::move(callbackFunction));
}

Response Client::Impl::get(const Request& request) {
    _logger->info("GET: {}", request.get_url().data());
    return RequestHandler{request, http::verb::get, _tp->get_executor()}.send();
}

Response Client::Impl::post(const Request& request) {
    _logger->info("POST: {}", request.get_url().data());
    return RequestHandler{request, http::verb::post, _tp->get_executor()}.send();
}

#pragma endregion // Client Implementation

Client::Client()
    : _impl{ new Impl() }
{}

void Client::get_async(const tg::rest::Request& request, Callback cb) {
    _impl->get_async(request, std::move(cb));
}

void Client::post_async(const tg::rest::Request& request, Callback cb) {
    _impl->post_async(request, std::move(cb));
}

Response Client::get(const tg::rest::Request& request) {
    return _impl->get(request);
}

Response Client::post(const tg::rest::Request& request) {
    return _impl->post(request);
}

Client::~Client() = default;
}

