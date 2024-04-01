#pragma once

#include "tgapi/command/function.h"
#include "tgapi/types/api_types.h"

#include "log/types.h"

namespace tg {

class TelegramBot;

bool parse_command_from_text(const Message& m, const TelegramBot& bot, std::string& command, std::string& args);
bool parse_command_arguments(const function::FunctionBase& func, std::string_view arguments, function::ArgumentList& args);

class BotInteraction {

public:

    BotInteraction(TelegramBot& bot, Message& message)
        : _bot{ bot }
        , _m{ message }
    {}

    BotInteraction() = delete;
    BotInteraction(const BotInteraction&) = delete;
    BotInteraction(BotInteraction&&) = delete;
    BotInteraction& operator=(const BotInteraction&) = delete;
    BotInteraction& operator=(BotInteraction&&) = delete;

    [[nodiscard]] const Chat& get_chat() const { return _m.Chat; }
    [[nodiscard]] const Message& get_message() const { return _m; }
    [[nodiscard]] TelegramBot& get_bot() const { return _bot; }

    [[maybe_unused]] Future<Result<Message>> reply_async(
            std::string_view text = "",
            const MessageEntities* entities = nullptr
    ) const;

private:
    TelegramBot& _bot;
    Message& _m;
};

class BotInteractionModuleBase {
public:

    BotInteractionModuleBase();
    BotInteractionModuleBase(const BotInteractionModuleBase&) = delete;
    BotInteractionModuleBase& operator=(const BotInteractionModuleBase&) = delete;
    virtual ~BotInteractionModuleBase() = default;

    virtual void execute_interaction(tg::UniquePtr<BotInteraction> interaction);
    virtual void post_login(tg::TelegramBot& bot) { }
    void receive_message(tg::UniquePtr<BotInteraction> interaction);

protected:

    mylog::Logger& get_logger() const;

    virtual void on_receive_message() { }

    [[nodiscard]] const BotInteraction& get_current_interaction() {
        if (!_current) {
            throw std::runtime_error("interaction is not set");
        }
        return *_current;
    }

    template<typename Class, typename ReturnType, typename... Args>
    void add_command(std::string commandName, ReturnType (Class::*func)(Args...)) {
        using FuncType = function::Function<ReturnType, Class, Args...>;

        auto ptr = make_unique<FuncType>((Class*)this, func);;
        _mapping[std::move(commandName)] = std::move(ptr);
    }


private:
    UniquePtr<BotInteraction> _current;
    mylog::LoggerPtr _logger;
    std::unordered_map<std::string, UniquePtr<tg::function::FunctionBase>> _mapping;
};

}