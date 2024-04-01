#include "tgapi/command/command_module.h"

#include "tgapi/bot/bot.h"

#include "log/logging.h"

namespace tg {

bool parse_command_from_text(
          const Message& m
        , const TelegramBot& bot
        , std::string& command
        , std::string& args
)
{
    command.clear();
    args.clear();

    auto it = std::find_if(m.Entites.begin(), m.Entites.end(), [](const MessageEntity& e) {
        return e.Offset == 0 && e.Type == MessageEntity::BOT_COMMAND;
    });

    if (it != m.Entites.end())
    {
        if (it->Length == 0) { return false; }

        size_t offset = 0;
        size_t length = it->Length;

        while(length > 0 && m.Text[offset] == '/') {
            ++offset;
            --length;
        }

        const long mentionPos = m.Text.find('@');
        if (mentionPos != std::string::npos) {
            if (mentionPos + 1 == m.Text.length()) { // malformed mention like '/command@' where symbol is at end of str
                return false;
            }
            // we have been mentioned
            const std::string_view mention{ m.Text.c_str() + mentionPos + 1, m.Text.length() - mentionPos - 1 };
            if (mention != bot.get_profile().UserName) { // command is not related to us
                return false;
            }
            command = m.Text.substr(offset, mentionPos - 1); // substring until mention symbol

        } else {
            command = m.Text.substr(offset, length);
        }

        //
        // example:
        //
        //  /cmd bar
        //  command name ends at 'd' character, if we can step 2 chars forward, possibly we have arguments (char b)
        if (offset + length + 1 <= m.Text.length()) {
            // parse arguments
            args = m.Text.substr(offset + length + 1);
        }

        return true;
    }

    return false;
}

bool parse_command_arguments(
          const function::FunctionBase& func
        , std::string_view arguments
        , function::ArgumentList& args
)
{
    return func.parse_arguments(arguments, /*out*/ args);
}

Future<Result<Message>> BotInteraction::reply_async(
    std::string_view text,
    const MessageEntities* entities
) const {
    SendMessageParams parms;
    parms.ChatId = _m.Chat.Id;
    parms.Text = text;
    if (entities) {
        parms.Entities = *entities;
    }
    ReplyParameters reply;
    reply.MessageId = _m.Id;
    parms.Reply = reply;

    return _bot.send_message_async(parms);
}

mylog::Logger& BotInteractionModuleBase::get_logger() const {
    return *_logger;
}

void BotInteractionModuleBase::receive_message(tg::UniquePtr<BotInteraction> interaction) {
    _current = std::move(interaction);
    on_receive_message();
    _current.reset();
}

void BotInteractionModuleBase::execute_interaction(UniquePtr<BotInteraction> interaction) {
    _current = std::move(interaction);
    try {
        std::string command;
        std::string argsList;
        function::ArgumentList args;
        if (parse_command_from_text(_current->get_message(), _current->get_bot(), /*out*/ command, /*out*/ argsList)) {
            get_logger().info(R"(Received interaction "{}")", command);

            auto it = _mapping.find(command);
            if (it != _mapping.end()) {

                if (parse_command_arguments(*it->second, argsList, /*out*/ args)) {
                    (*it->second)(args);
                    get_logger().info(R"(Interaction "{}" OK [Args = "{}"  Num = {}])", command, argsList, args.size());

                } else {
                    get_logger().error(R"(Interaction "{}" failed: not enough arguments)", command);
                }
            }
        }
    } catch (const std::exception& e) {
        // ...
        get_logger().error("Exception occurred while interaction execution: {}", e.what());
    }
    _current.reset();
}

BotInteractionModuleBase::BotInteractionModuleBase() {
    _logger = mylog::LogManager::get().create_logger("Interaction");
}

}