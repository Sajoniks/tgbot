#include "tgapi/bot/bot.h"
#include "tgapi/rest_client.h"
#include "tgapi/types/api_types_parse.h"

#include "log/logging.h"
#include "util.h"
#include "sqlite/sqlite.h"

#include <list>
#include <thread>
#include <fmt/core.h>
#include <boost/asio/thread_pool.hpp>
#include <filesystem>
#include <utility>


namespace tg
{

#pragma region Parse

namespace parse {

auto do_parse(const SendMessageParams& p, ParseTag<JValue>, JAlloc& a) {
    JValue o{ rapidjson::kObjectType };
    {
        o.AddMember("chat_id", do_parse<JValue>(p.ChatId, a), a);
        o.AddMember("text", JValue{ p.Text.c_str(), a }, a);
        if (p.Entities) {

        }
        if (p.Reply) {
            o.AddMember("reply_parameters", do_parse<JValue>(*p.Reply, a), a);
        }
    }
    return o;
}

}

#pragma endregion // Parse


#pragma region Timer service

namespace detail {

class TimerData {
public:

    using HandleType    = long;
    using ClockType     = typename boost::posix_time::microsec_clock;
    using TimePointType = typename boost::posix_time::ptime;
    using IntervalType  = typename boost::posix_time::time_duration;
    using CallbackType  = typename std::function<void(TimerReply&)>;

    TimerData(HandleType handle, CallbackType cb, IntervalType interval, const TimePointType& tp, bool looping)
        : _expiresAt{ tp }
        , _cb { std::move(cb) }
        , _interval{ interval }
        , _handle { handle }
        , _loop{ looping }
    {}
    TimerData(HandleType handle, CallbackType cb, IntervalType interval, bool looping)
        : _expiresAt{ ClockType::universal_time() + interval }
        , _cb { std::move(cb) }
        , _interval{ interval }
        , _handle { handle }
        , _loop{ looping }
    {}
    TimerData(HandleType handle, CallbackType cb, long intervalSeconds, bool looping)
        : TimerData(handle, cb, boost::posix_time::seconds(intervalSeconds), looping)
    {}

    TimerData(const TimerData&) = default;
    TimerData(TimerData&&) = default;
    TimerData& operator=(const TimerData&) = default;
    TimerData& operator=(TimerData&&) = default;

    [[nodiscard]] TimePointType expires_at() const { return _expiresAt; }

    void refresh() {
        _expiresAt = ClockType::universal_time() + _interval;
    }

    void refresh(IntervalType time) {
        _interval = std::move(time);
        refresh();
    }

    void refresh(long seconds) {
        _interval = boost::posix_time::seconds(seconds);
        refresh();
    }

    [[nodiscard]] bool looping() const { return _loop; }
    [[nodiscard]] long handle() const { return _handle; }

    void operator()(TimerReply& r) {
        _cb(r);
    }

    [[nodiscard]] auto time_remaining() const {
        return expires_at() - ClockType::universal_time();
    }

    [[nodiscard]] auto interval() -> IntervalType const {
        return _interval;
    }

private:
    TimePointType _expiresAt;
    HandleType _handle;
    boost::posix_time::time_duration _interval;
    CallbackType _cb;
    bool _loop;
};

bool operator> (const TimerData& a, const TimerData& b) {
    return a.expires_at() > b.expires_at();
}

bool operator< (const TimerData& a, const TimerData& b) {
    return a.expires_at() < b.expires_at();
}

bool operator==(const TimerData& a, const long h) {
    return a.handle() == h;
}

class TimerServiceImpl {

    void worker_thread() {

        namespace chrono = std::chrono;

        while(true) {

            {
                std::unique_lock<std::recursive_mutex> lock { _mutex };
                if (_timers.empty()) {
                    // sleep
                    lock.unlock();

                    std::unique_lock<std::mutex> workerLock { _workerMutex };
                    _logger->info("Waiting for timers");
                    _cv.wait(workerLock);

                    _logger->info("Signal process timers");
                    continue;
                }
            }

            {
                bool needSort = false;

                const auto now = TimerData::ClockType::universal_time();

                std::unique_lock<std::recursive_mutex> lock { _mutex };
                for (auto it = _timers.begin(); it != _timers.cend();) {
                    auto& data = *it;
                    auto handle = data.handle();
                    const auto diff = (data.expires_at() - now).total_milliseconds();
                    if (diff < 0) {

                        TimerReply r{ handle };
                        // @todo Post on thread pool?
                        data(r);

                        if (r.is_delete()) {
                            _logger->info("Deleted timer h = {}", data.handle());
                            it = _timers.erase(it);
                            continue;
                        }
                        else if (r.is_interval()) {
                            data.refresh(r.interval());
                            _logger->info("Updated timer h = {} new = {}s", data.handle(), r.interval());
                        }

                        if (data.looping()) {
                            data.refresh();
                            needSort = true;
                            _logger->info("Refresh timer h = {}", data.handle());
                        }
                        else {
                            // timer has expired
                            _logger->info("Erased timer h = {}", data.handle());
                            it = _timers.erase(it);
                        }

                    } else {
                        lock.unlock();

                        const long sleepTime = std::max(data.time_remaining().total_seconds(), 0l);

                        if (sleepTime > 0) {
                            _logger->info("All timers are in process. Went sleeping for {}s", sleepTime);

                            std::unique_lock<std::mutex> workerLock{_workerMutex};
                            _waiting = true;
                            _cv.wait_for(workerLock, chrono::seconds(sleepTime), [this] {
                                return !_waiting;
                            });
                        }

                        break;
                    }
                }

                if (needSort) {
                    _timers.sort();
                }
            }
        }
    }

public:

    TimerServiceImpl() {
        _logger = mylog::LogManager::get().create_logger("Timers");
        _thread = std::thread{ &TimerServiceImpl::worker_thread, this };
        _thread.detach();
    }

    TimerServiceImpl(const TimerServiceImpl&) = delete;
    TimerServiceImpl(TimerServiceImpl&&) = delete;
    const TimerServiceImpl& operator=(const TimerServiceImpl&) = delete;
    const TimerServiceImpl& operator=(TimerServiceImpl&&) = delete;

    void add_timer(const std::function<void(TimerReply&)>& callback, long handle, long interval, bool looping) {
        std::unique_lock<std::recursive_mutex> lock{ _mutex };

        _timers.emplace_back(handle, callback, interval, looping);
        _timers.sort();

        _logger->info("Added timer h = {} interval = {}s", handle, interval);

        {
            std::unique_lock<std::mutex> workerLock { _workerMutex };
            _waiting = false;
            _cv.notify_one();
        }
    }

    void update_timer(long handle, long interval) {
        std::unique_lock<std::recursive_mutex> lock{ _mutex };
        auto it = std::find(_timers.begin(), _timers.end(), handle);
        if (it != _timers.end()) {
            auto& data = *it;
            data.refresh(interval);

            _logger->info("Updated timer h = {} new = {}s", handle, interval);

            {
                std::unique_lock<std::mutex> workerLock { _workerMutex };
                _waiting = false;
                _cv.notify_one();
            }
        }
    }

    bool delete_timer(long handle) {
        std::unique_lock<std::recursive_mutex> lock{ _mutex };

        auto it = std::find(_timers.cbegin(), _timers.cend(), handle);
        if (it != _timers.cend()) {
            _timers.erase(it);
            _timers.sort();

            {
                std::unique_lock<std::mutex> workerLock { _workerMutex };
                _waiting = false;
                _cv.notify_one();
            }

            return true;
        }

        return false;
    }

    ~TimerServiceImpl() = default;

private:
    std::thread _thread;
    std::recursive_mutex _mutex;

    std::mutex _workerMutex;
    bool _waiting { false };
    std::condition_variable _cv;

    mylog::LoggerPtr _logger;

    std::list<TimerData> _timers; // we could use priority_queue, but we need iterators
};

}

void TimerService::add_timer(const std::function<void(TimerReply&)>& callback, long handle, long interval, bool looping) {
    _service->add_timer(callback, handle, interval, looping);
}

bool TimerService::delete_timer(long handle) {
    return _service->delete_timer(handle);
}

void TimerService::update_timer(long handle, long interval) {
    _service->update_timer(handle, interval);
}

TimerService::TimerService()
    : _service { new detail::TimerServiceImpl() }
{}

TimerService::~TimerService() = default;

#pragma endregion // Timer service

TelegramBot::TelegramBot(config::Store  config, std::unique_ptr<BotInteractionModuleBase> interaction)
    : _restClient{ nullptr }
    , _config{ std::move( config ) }
    , _timerService{ new TimerService() }
    , _botInteraction{ std::move(interaction) }
    , _isLogged{ false }
{
    _logger = mylog::LogManager::get().create_logger("Bot");

    const auto telegramToken = _config["Telegram::Token"];
    if (telegramToken.empty()) {
        throw std::runtime_error("Token was not found in configuration");
    }
    _token = fmt::format("bot{}", telegramToken);

    _gateway = _config["Telegram::Gateway"];
    if (_gateway.empty()) {
        throw std::runtime_error("Telegram gateway was not found in configuration");
    }

    int numThreads;
    {
        try {
            numThreads = std::stoi(_config["Telegram::Threads"].data());
        } catch (const std::invalid_argument& e) {
            _logger->error("Exception while configuring bot: {} (received: {})", e.what(),
                           _config["Telegram::Threads"]);
            throw e;
        }
    }

    int longPollInterval;
    {
        try {
            auto interval = _config["Telegram::LongPolling::Interval"];
            if (!interval.empty()) {
                _longPollInterval = std::stoi(interval.data());
            }
        } catch (const std::invalid_argument& e) {
            _logger->error("Exception while configuring bot: {} (received: {})", e.what(), _config["Telegram::LongPolling::Interval"]);
            throw e;
        }
    }

    _logger->info("Gateway: {}", _gateway);
    _logger->info("Long-Polling interval: {}s", _longPollInterval);

    auto threadPool = std::make_unique<boost::asio::thread_pool>(numThreads);

    _executor = std::make_unique<boost::asio::any_io_executor>(threadPool->get_executor());
    _ctx = std::move(threadPool);
    _restClient = std::make_unique<rest::Client>(*_executor);
}

rest::Request TelegramBot::createBotRestRequest() {
    rest::Request request{_gateway};
    request.segments().push_back(_token);
    return request;
}

std::future<Result<User>> TelegramBot::login_async()
{
    if (_isLogged)
    {
        throw std::runtime_error("bot is already logged");
    }
    _isLogged = true;

    _botInteraction->post_login(*this);

    auto promise = std::make_shared<std::promise<Result<User>>>();

    boost::asio::post(*_executor, [this, promise]{
        auto request = createBotRestRequest();
        request.segments().push_back("getMe");

        try {
            const auto& response = _restClient->get(request);

            auto profile = tg::parse::do_parse<Result<User>>(response.get_json()->GetObj());
            if (!profile.is_ok())
            {
                _isLogged = false;
            }
            else {
                _profile = *profile.content();
                _logger->info(R"(Logged as "{}")", _profile.UserName);
            }

            promise->set_value(std::move(profile));
        }
        catch (const std::exception& e) {
            _isLogged = false;
        }
    });

    return promise->get_future();
}

void TelegramBot::get_updates_async() {
    assert_if_not_logged();

    boost::asio::post(*_executor, [this] {

        auto request = createBotRestRequest();
        request.segments().push_back("getUpdates");
        request.params().set("offset", std::to_string(_lastReceivedUpdate + 1));

        try {
            const auto& response = _restClient->get(request);

            auto pollUpdates = parse::do_parse<BotGetUpdates>(response.get_json()->GetObj());
            if (pollUpdates.OK) {
                auto prevReceivedUpdate = _lastReceivedUpdate;

                for (auto& upd: pollUpdates.Updates) {
                    if (upd.UpdateType == BotUpdate::MESSAGE) {
                        auto& messageData = upd.UpdateData.Message;
                        for (auto& messageEntity: messageData.Entites) {
                            if (messageEntity.Type == MessageEntity::BOT_COMMAND) {
                                auto interaction = make_unique<BotInteraction>(this, &messageData);
                                _botInteraction->execute_interaction(std::move(interaction));
                            }
                        }
                    }

                    _lastReceivedUpdate = upd.Id;
                }

                if (_lastReceivedUpdate != prevReceivedUpdate) {
                    _tmpFile << _lastReceivedUpdate;
                    _tmpFile.seekp(0);
                    std::flush(_tmpFile);

                    _logger->info("Last received update = {}", _lastReceivedUpdate);
                }
            }
        }
        catch (const std::exception& e) {
            // ...
            _logger->error("Exception occurred while processing updates: {}", e.what());
        }

        schedule_next_poll();
    });
}

void TelegramBot::begin_long_polling() {
    assert_if_not_logged();

    if (_isLongPolling) {
        throw std::runtime_error("already long polling");
    }
    _isLongPolling = true;

    _logger->info("Running long polling mode");

    namespace fs = std::filesystem;

    // manage cache
    // we write last received update into the file in order to receive valid updates

    fs::path basePath = util::get_executable_path();
    fs::path tempDirPath = basePath / "temp";
    fs::path tgFile = tempDirPath / "poll.info";

    if (!fs::exists(tempDirPath)) {
        fs::create_directories(tempDirPath);
    }
    if (!fs::exists(tgFile)) {
        std::ofstream ofs;
        ofs.open(tgFile);
        ofs << 0;
        std::flush(ofs);
        _logger->info(R"(Created long polling cache file at "{}")", tgFile.c_str());
    } else {
        std::ifstream  ifs;
        ifs.open(tgFile);
        if (!ifs.eof())  {
            ifs >> _lastReceivedUpdate;
            _logger->info("Read cache last update = {}", _lastReceivedUpdate);
        }
    }
    _tmpFile.open(tgFile, std::ios_base::out | std::ios_base::trunc );
    _tmpFile << _lastReceivedUpdate;
    _tmpFile.seekp(0);
    std::flush(_tmpFile);

    _getUpdatesTimer = std::make_unique<boost::asio::steady_timer>( *_executor);

    get_updates_async();

    std::mutex m;
    std::unique_lock<std::mutex> lock{m };
    std::condition_variable v;
    v.wait(lock);
}

const User& TelegramBot::get_profile() const {
    return _profile;
}

void TelegramBot::schedule_next_poll() {
    if (_getUpdatesTimer->expires_from_now().count() <= 0) {
        _getUpdatesTimer->expires_from_now(std::chrono::seconds(_longPollInterval));
    }
    _getUpdatesTimer->async_wait(std::bind(&TelegramBot::on_poll_update_timer, this, std::placeholders::_1));
}

void TelegramBot::on_poll_update_timer(const boost::system::error_code&) {
    get_updates_async();
}

void TelegramBot::assert_if_not_logged() const {
    if (!_isLogged) throw std::runtime_error("login was not called");
}

Future<Result<Message>> TelegramBot::send_message_async(const ChatId& chatId, std::string_view message) {
    SendMessageParams p;
    p.ChatId = chatId;
    p.Text = message;
    return send_message_async(p);
}

std::future<Result<Message>> TelegramBot::send_message_async(const SendMessageParams& parms) {
    auto promise = std::make_shared<std::promise<Result<Message>>>();

    if (parms.Text.empty()) {
        promise->set_value( Result<Message>::from_error("Message cannot be empty") );
        return promise->get_future();
    }

    boost::asio::post(*_executor, [this, parms, promise] {
        auto req = createBotRestRequest();
        req.segments().push_back("sendMessage");

        req.set_json_content(parms);

        auto r = _restClient->post(req);

        auto result = parse::do_parse<Result<Message>>(r.get_json()->GetObj());
        promise->set_value(std::move(result));
    });

    return promise->get_future();
}

const config::Store& TelegramBot::get_config() const {
    return _config;
}

TimerService& TelegramBot::get_timer_service() const {
    return *_timerService;
}

TimerReply::TimerReply(long handle)
    : _handle{ handle } {

}

void TimerReply::set_delete() {
    consume_reply();
    _delete = true;
}

void TimerReply::update_interval(long time) {
    consume_reply();
    _newInterval = std::max(time, 1l);
}

void TimerReply::consume_reply() {
    assert(!_consumed);
    _consumed = true;
}

bool TimerReply::is_interval() const { return _newInterval > 0; }
bool TimerReply::is_delete() const { return _delete; }
long TimerReply::interval() const { return _newInterval; }
long TimerReply::handle() const { return _handle; }

}
