#include "tgapi/rest_client.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl.hpp>

#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "log/logging.h"

namespace tg::rest {


namespace detail {
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

RequestHandler::RequestHandler(Client* client, Request request, http::verb verb, asio::any_io_executor* executor)
    : _request{ std::move(request) }
    , _verb{ verb }
    , _executor{ executor }
    , _resolver{ *executor }
    , _sslContext{ ssl::context::tlsv12_client }
    , _owner{ client }
    , _stream{ *executor, _sslContext }
{
    _sslContext.set_default_verify_paths();
    _sslContext.set_verify_mode(ssl::verify_peer);
    _sslContext.set_verify_callback(ssl::host_name_verification(_request.get_url().host()));
}

void RequestHandlerBase::notify_completion(tg::rest::Client* client) {
    client->request_completed(*this);
}

Future<Response> RequestHandler::send_async() {
    using namespace std::placeholders;

    if (!SSL_set_tlsext_host_name(_stream.native_handle(), _request.get_url().host().c_str()))
    {
        _promise.set_exception( std::make_exception_ptr(std::runtime_error("ssl Error")));
        return _promise.get_future();
    }

    auto call = std::bind(&RequestHandler::on_resolve, this, _1, _2);
    _resolver.async_resolve(_request.get_url().host(), _request.get_url().scheme(), call);

    return _promise.get_future();
}

void RequestHandler::on_resolve(const system::error_code& ec, const tcp::resolver::results_type& r) {
    using namespace std::placeholders;

    if (ec) {
        _promise.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
    }

    auto call = std::bind(&RequestHandler::on_connect, this, _1, _2);
    beast::get_lowest_layer(_stream).async_connect(r, call);
}

void RequestHandler::on_connect(const system::error_code& ec, const tcp::endpoint& ep) {
    using namespace std::placeholders;

    if (ec){
        _promise.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
    }

    auto call = std::bind(&RequestHandler::on_handshake, this, _1);
    _stream.async_handshake(ssl::stream_base::client, call);
}

void RequestHandler::on_handshake(const system::error_code& ec) {
    using namespace std::placeholders;

    if (ec) {
        _promise.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
    }

    http::request<http::string_body> req{ _verb, _request.get_url().encoded_target(), 11 };
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::host, _request.get_url().host());

    system::error_code writeEc;
    const auto written = http::write(_stream, req, writeEc);

    on_sent(writeEc, written);
}

void RequestHandler::on_sent(const system::error_code& ec, const size_t) {

    if (ec) {
        _promise.set_exception(std::make_exception_ptr(std::runtime_error(ec.message())));
        return;
    }

    http::response<http::string_body> res;
    http::read(_stream, _buffer, res);

    Response r{res };
    auto promise = std::move(_promise);

    notify_completion(_owner);

    promise.set_value(std::move(r));
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

    return Response{res };
}
}

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

Response::Response(const http::response<http::string_body>& body) {
    _doc = std::make_shared<rapidjson::Document>();

    rapidjson::StringStream ss{ body.body().c_str() };
    _doc->ParseStream(ss);
}

const JDoc* Response::get_json() const {
    return _doc.get();
}

Client::Client(asio::any_io_executor& ctx)
    : _executor{ &ctx }
{
    _logger = mylog::LogManager::get().create_logger("Rest");
}

std::future<Response> Client::get_async(const Request& request) {
    _logger->info("GET: {}", request.get_url().data());
    auto handler = make_shared<detail::RequestHandler>(this, request, http::verb::get, _executor);
    auto handlerPtr = handler.get();
    _requests.push_back(std::move(handler));

    return handlerPtr->send_async();
}

std::future<Response> Client::post_async(const Request& request) {
    _logger->info("POST: {}", request.get_url().data());
    auto handler = make_shared<detail::RequestHandler>(this, request, http::verb::post, _executor);
    auto handlerPtr = handler.get();
    _requests.push_back(std::move(handler));

    return handlerPtr->send_async();
}

Response Client::get(const Request& request) {
    _logger->info("GET: {}", request.get_url().data());
    return detail::RequestHandler{this, request, http::verb::get, _executor}.send();
}

Response Client::post(const Request& request) {
    _logger->info("POST: {}", request.get_url().data());
    return detail::RequestHandler{this, request, http::verb::post, _executor}.send();
}

void Client::request_completed(detail::RequestHandlerBase& req) {
    auto* ptr = &req;
    _requests.erase(std::remove_if(
            _requests.begin(),
            _requests.end(), [ptr](const SharedPtr<detail::RequestHandlerBase>& p) { return ptr == p.get(); }
    ), _requests.end());
}

}

