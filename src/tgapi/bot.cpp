#include "tgapi/bot/bot.h"
#include "tgapi/rest_client.h"
#include "tgapi/fmt/tgbot_fmt.h"
#include "tgapi/types/api_types_parse.h"

#include "log/logging.h"
#include "util.h"
#include "sqlite/sqlite.h"

#include <list>
#include <thread>
#include <boost/asio/thread_pool.hpp>
#include <filesystem>
#include <utility>
#include <iostream>

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
            o.AddMember("entities", do_parse<JValue>(*p.Entities, a), a);
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
        : TimerData(handle, std::move(cb), boost::posix_time::seconds(intervalSeconds), looping)
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
        _interval = time;
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

    [[nodiscard]] IntervalType interval() const {
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

}

class TimerService::Impl {

    void worker_thread() {
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

                const auto now = detail::TimerData::ClockType::universal_time();

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
                            const auto newInterval = boost::posix_time::milliseconds(r.interval());
                            data.refresh(newInterval);

                            _logger->info("Updated timer h = {} new = {}s", data.handle(), newInterval.total_seconds());
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

                        const long sleepTime = std::max<long>(data.time_remaining().total_seconds(), 0l);

                        if (sleepTime > 0) {
                            _logger->info("All timers are in process. Went sleeping for {}s", sleepTime);

                            std::unique_lock<std::mutex> workerLock{_workerMutex};
                            _waiting = true;
                            _cv.wait_for(workerLock, std::chrono::seconds(sleepTime), [this] {
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

    Impl() {
        _logger = mylog::LogManager::get().create_logger("Timers");
        _thread = std::thread{ &Impl::worker_thread, this };
        _thread.detach();
    }

    Impl(const Impl&) = delete;
    Impl(Impl&&) = delete;
    const Impl& operator=(const Impl&) = delete;
    const Impl& operator=(Impl&&) = delete;

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

    ~Impl() = default;

private:
    std::thread _thread;
    std::recursive_mutex _mutex;

    std::mutex _workerMutex;
    bool _waiting { false };
    std::condition_variable _cv;

    mylog::LoggerPtr _logger;

    std::list<detail::TimerData> _timers; // we could use priority_queue, but we need iterators
};

void TimerService::add_timer(const std::function<void(TimerReply&)>& callback, long handle, long interval, bool looping) {
    _impl->add_timer(callback, handle, interval, looping);
}

bool TimerService::delete_timer(long handle) {
    return _impl->delete_timer(handle);
}

void TimerService::update_timer(long handle, long interval) {
    _impl->update_timer(handle, interval);
}

TimerService::TimerService()
    : _impl { new Impl() }
{}

TimerService::~TimerService() = default;

#pragma endregion // Timer service

class TelegramBot::Impl
{
    void schedule_next_poll();
    void get_updates_async();
    void assert_if_not_logged() const;

    rest::Request createBotRestRequest();

public:

    Impl(TelegramBot& owner, config::Store config, mylog::LoggerPtr logger, UniquePtr<BotInteractionModuleBase> interaction);

    Impl(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() = default;

    void begin_long_polling();

    Future<Result<User>>      login_async();
    Future<Result<Message>>   send_message_async(const SendMessageParams& parms);

    [[nodiscard]] const User& get_profile() const;
    [[nodiscard]] const config::Store& get_config() const;
    [[nodiscard]] TimerService& get_timer_service() const;

private:

    std::condition_variable _isTerminating;

    std::string _token;
    std::string _gateway;

    config::Store _config;

    UniquePtr<boost::asio::steady_timer> _getUpdatesTimer { nullptr };
    UniquePtr<rest::Client> _restClient { nullptr };
    UniquePtr<BotInteractionModuleBase> _botInteraction { nullptr };
    UniquePtr<TimerService> _timerService { nullptr };

    std::thread _workerThread;
    boost::asio::io_context _ioCtx;

    mylog::LoggerPtr _logger { nullptr };
    TelegramBot* _interface { nullptr };

    std::fstream _tmpFile;
    User _profile;

    long _lastReceivedUpdate { 0 };
    int _longPollInterval { 5 };

    bool _isLongPolling { false };
    bool _isLogged { false };
};


TelegramBot::TelegramBot(config::Store config, std::unique_ptr<BotInteractionModuleBase> interaction)
    : _impl{ new Impl(*this, std::move(config), mylog::LogManager::get().create_logger("Bot"), std::move(interaction)) }
{}

Future<Result<User>> TelegramBot::login_async() {
    return _impl->login_async();
}

Future<Result<Message>> TelegramBot::send_message_async(const tg::SendMessageParams& parms) {
    return _impl->send_message_async(parms);
}

Future<Result<Message>> TelegramBot::send_message_async(const ChatId& chatId, std::string_view message) {
    SendMessageParams p;
    p.ChatId = chatId;
    p.Text = message;
    return send_message_async(p);
}

void TelegramBot::begin_long_polling() {
    _impl->begin_long_polling();
}

const User& TelegramBot::get_profile() const {
    return _impl->get_profile();
}

const config::Store& TelegramBot::get_config() const {
    return _impl->get_config();
}

TimerService& TelegramBot::get_timer_service() const {
    return _impl->get_timer_service();
}

TelegramBot::~TelegramBot() {

}

#pragma region TgBot Implementation

TelegramBot::Impl::Impl(
      TelegramBot& owner
    , config::Store config
    , mylog::LoggerPtr logger
    , UniquePtr<BotInteractionModuleBase> interaction
)
    : _config{ std::move(config) }
    , _botInteraction{ std::move(interaction) }
    , _logger{ logger }
    , _timerService{ make_unique<TimerService>() }
    , _restClient{ make_unique<rest::Client>() }
    , _interface{ &owner }
{
    namespace asio = boost::asio;

    const std::string_view telegramToken = _config["Telegram::Token"];
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
}

rest::Request TelegramBot::Impl::createBotRestRequest() {
    rest::Request request{_gateway};
    request.segments().push_back(_token);
    return request;
}

std::future<Result<User>> TelegramBot::Impl::login_async()
{
    namespace asio = boost::asio;

    if (_isLogged)
    {
        throw std::runtime_error("bot is already logged");
    }

    auto promise = std::make_shared<std::promise<Result<User>>>();

    auto loginComplete = [this, promise](const rest::Response& r) {
        try {
            auto profile = tg::parse::do_parse<Result<User>>(r.get_json()->GetObj());
            if (profile.is_ok()) {

                _profile = *profile.content();
                _botInteraction->post_login(*_interface);
                _isLogged = true;

                promise->set_value(profile);
            } else {
                _isLogged = false;
            }
        } catch (const std::exception& e) {
            _isLogged = false;
        }
    };

    rest::Request request = createBotRestRequest();
    request.segments().push_back("getMe");

    _restClient->get_async(request, loginComplete);

    return promise->get_future();
}

void TelegramBot::Impl::get_updates_async() {
    assert_if_not_logged();

    using Updates = Result<std::vector<BotUpdate>>;
    rest::Request request = createBotRestRequest();
    request.segments().push_back("getUpdates");
    request.params().set("offset", std::to_string(_lastReceivedUpdate + 1));

    _restClient->get_async(request, [this](const rest::Response& r) {
        try {
            auto updatesResult = parse::do_parse<Updates>(r.get_json()->GetObj());
            if (!updatesResult) {
                _logger->error("getUpdates error: {}", *updatesResult.error());
            } else {
                std::vector<BotUpdate>& updates = *updatesResult.content();
                const long prevUpdate = _lastReceivedUpdate;

                for (auto&& upd: updates) {
                    if (upd.UpdateType == BotUpdate::MESSAGE) {
                        bool commandHandled = false;

                        Message& messageData = upd.UpdateData.Message;
                        auto interaction = make_unique<BotInteraction>(*_interface, messageData);

                        for (const MessageEntity& messageEntity: messageData.Entites) {
                            if (messageEntity.Type == MessageEntity::BOT_COMMAND) {
                                _botInteraction->execute_interaction(std::move(interaction));
                                commandHandled = true;
                                break;
                            }
                        }

                        if (!commandHandled) {
                            _botInteraction->receive_message(std::move(interaction));
                        }
                    }
                }

                if (_lastReceivedUpdate != prevUpdate) {
                    _tmpFile << _lastReceivedUpdate;
                    _tmpFile.seekp(0);
                    std::flush(_tmpFile);

                    _logger->info("Last received update = {}", _lastReceivedUpdate);
                }
            }
        } catch(const std::exception& e) {
            _logger->error("Exception occurred while processing updates: {}", e.what());
        }

        schedule_next_poll();
    });
}

void TelegramBot::Impl::begin_long_polling() {
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
        _logger->info(R"(Created long polling cache file at "{}")", tgFile);
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

    auto guard = boost::asio::make_work_guard(_ioCtx);
    _getUpdatesTimer = std::make_unique<boost::asio::steady_timer>(_ioCtx.get_executor());

    _workerThread = std::thread{ [this]{
        _ioCtx.run();
    } };

    get_updates_async();

    {
        std::mutex blockingMutex;
        std::unique_lock lock(blockingMutex);
        _isTerminating.wait(lock);
    }
}

const User& TelegramBot::Impl::get_profile() const {
    return _profile;
}

void TelegramBot::Impl::schedule_next_poll() {
    if (_getUpdatesTimer->expires_from_now().count() <= 0) {
        _getUpdatesTimer->expires_from_now(std::chrono::seconds(_longPollInterval));
    }
    _getUpdatesTimer->async_wait([this](const system::error_code& ec) {
        if (!ec) {
            get_updates_async();
        }
    });
}

void TelegramBot::Impl::assert_if_not_logged() const {
    if (!_isLogged) throw std::runtime_error("login was not called");
}

std::future<Result<Message>> TelegramBot::Impl::send_message_async(const SendMessageParams& parms) {
    auto promise = std::make_shared<std::promise<Result<Message>>>();

    if (parms.Text.empty()) {
        promise->set_value( Result<Message>::from_error("Message cannot be empty") );
        return promise->get_future();
    }

    rest::Request request = createBotRestRequest();
    request.segments().push_back("sendMessage");
    request.set_json_content(parms);
    _restClient->post_async(request, [this, promise](const rest::Response& r) {
        auto result = parse::do_parse<Result<Message>>(r.get_json()->GetObj());
        if (!result) {
            _logger->error("sendMessage error: {}", *result.error());
        } else {
            promise->set_value(std::move(result));
        }
    });

    return promise->get_future();
}

const config::Store& TelegramBot::Impl::get_config() const {
    return _config;
}

TimerService& TelegramBot::Impl::get_timer_service() const {
    return *_timerService;
}

#pragma endregion // TgBot Implementation


TimerReply::TimerReply(long handle)
    : _handle{ handle } {

}

void TimerReply::set_delete() {
    consume_reply();
    _delete = true;
}

void TimerReply::update_interval(long time) {
    consume_reply();
    _newIntervalMs = std::max(time, 1l);
}

void TimerReply::consume_reply() {
    assert(!_consumed);
    _consumed = true;
}

bool TimerReply::is_interval() const { return _newIntervalMs > 0; }
bool TimerReply::is_delete() const { return _delete; }
long TimerReply::interval() const { return _newIntervalMs; }
long TimerReply::handle() const { return _handle; }

}
