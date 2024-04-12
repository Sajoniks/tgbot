#pragma once

#include <fstream>

#include "configuration/configuration.h"
#include "tgapi/command/command_module.h"
#include "tgapi/rest_client.h"
#include "tgapi/types/api_types.h"


namespace tg
{

struct SendMessageParams {
    tg::ChatId ChatId;
    std::string Text;
    std::optional<MessageEntities> Entities;
    std::optional<ReplyParameters> Reply;
};

class TimerReply final {
    void consume_reply();
public:

    explicit TimerReply(long handle);

    TimerReply(const TimerReply&) = delete;
    TimerReply(TimerReply&&) = delete;
    TimerReply& operator=(const TimerReply&) = delete;
    TimerReply& operator=(TimerReply&&) = delete;
    ~TimerReply() = default;

    void set_delete();
    void update_interval(long time);

    [[nodiscard]] bool is_delete() const;
    [[nodiscard]] bool is_interval() const;

    [[nodiscard]] long interval() const;
    [[nodiscard]] long handle() const;

private:
    bool _consumed { false };
    bool _delete { false };
    long _newIntervalMs { -1 };
    long _handle { 0 };
};

class TimerService final {

    class Impl;

public:

    TimerService();

    TimerService(const TimerService&) = delete;
    TimerService(TimerService&&) = delete;
    TimerService& operator=(const TimerService&) = delete;
    TimerService& operator=(TimerService&&) = delete;
    ~TimerService();

    void add_timer(const std::function<void(TimerReply&)>& callback, long handle, long interval, bool looping);
    void update_timer(long handle, long interval);
    bool delete_timer(long handle);

private:
    UniquePtr<Impl> _impl;
};

class TelegramBot final
{
    class Impl;

public:

    TelegramBot(config::Store config, UniquePtr<BotInteractionModuleBase> interaction);

    TelegramBot(const TelegramBot&) = delete;
    TelegramBot(TelegramBot&&) = delete;
    TelegramBot& operator=(const TelegramBot&) = delete;
    TelegramBot& operator=(TelegramBot&&) = delete;
    ~TelegramBot();

    [[maybe_unused]] Future<Result<User>>      login_async();
    [[maybe_unused]] Future<Result<Message>>   send_message_async(const SendMessageParams& parms);
    [[maybe_unused]] Future<Result<Message>>   send_message_async(const ChatId& chatId, std::string_view message);

    void begin_long_polling();

    [[nodiscard]] const User& get_profile() const;
    [[nodiscard]] const config::Store& get_config() const;

    [[nodiscard]] TimerService& get_timer_service() const;

private:
    UniquePtr<Impl> _impl;
};

}



