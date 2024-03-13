#include "tgapi/bot/bot.h"

#include "log/logging.h"
#include "configuration/configuration.h"

class ExampleInteraction : public tg::BotInteractionModuleBase {
public:
    void example_command(int x, float y) { }
    void example_command_2(const long* optional, const std::string& xy) { } // note: arguments are immutable so must be passed by const

    ExampleInteraction() {
        add_command("example1", &ExampleInteraction::example_command);
        add_command("example2", &ExampleInteraction::example_command_2);
    }
};


int main()
{
    auto appLogger = mylog::LogManager::get().create_logger("App");
    auto config = config::Store::from_json("config/config.json");

    mylog::LogManager::configure(config);

    auto interactionService = tg::make_unique<ExampleInteraction>();
    tg::TelegramBot bot{config, std::move(interactionService)};

    auto log = bot.login_async().get();
    if (log.is_ok()) {
        bot.begin_long_polling();
    }

    return 0;
}