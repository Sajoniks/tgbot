#pragma once

#include <cstdint>
#include <string>
#include <cassert>
#include <vector>
#include <optional>
#include <variant>

#include "tgapi/tgapi.h"


namespace tg {

template<typename T>
class Result {

    Result() = default;

public:

    static Result from_error(std::string description) {
        auto create = [&] {
            Result r;
            r._ok = false;
            r._error = std::move(description);
            return r;
        };
        return create();
    }

    static Result from_content(T&& content) {
        auto create = [&] {
            Result r;
            r._ok = true;
            r._content = std::forward<T>(content);
            return r;
        };
        return create();
    }

    Result(const Result&) = default;
    Result(Result&&) = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;
    ~Result() = default;

    [[nodiscard]] bool is_ok() const { return _ok; }
    [[nodiscard]] const T* content() const { return _ok ? (&*_content) : nullptr; };
    [[nodiscard]] const std::string* error() const { return _ok ? nullptr : (&*_error); }

private:
    std::optional<std::string> _error;
    std::optional<T> _content;
    bool _ok { false };
};

struct ReplyParameters {
    long MessageId { 0 };
};

struct MessageEntity {
    enum Type {
        MENTION,
        HASHTAG,
        CASHTAG,
        BOT_COMMAND,
        URL,
        EMAIL,
        PHONE_NUMBER
    };
    int Type { 0 };
    long Offset { 0 };
    long Length { 0 };
};

using MessageEntities = std::vector<MessageEntity>;

enum UserFlags {
    BOT = 1 << 0,
    CAN_JOIN_GROUPS = 1 << 1,
    CAN_READ_ALL_GROUP_MESSAGES = 1 << 2,
    SUPPORTS_INLINE_QUERIES = 1 << 3,
    PREMIUM = 1 << 4,
    ADDED_TO_ATTACHMENT_MENU = 1 << 5
};

struct User {
    long Id { 0 };
    std::uint32_t Flags { 0 };
    std::string FirstName;
    std::string LastName;
    std::string UserName;
    std::string LanguageTag;
};

inline bool user_flag_set(const User& pf, UserFlags f) {
    return pf.Flags & f;
}

using ChatId = std::variant<std::string, long>;

struct Chat {
    ChatId Id;
};

struct Message {
    long Id { 0 };
    User From;
    tg::Chat Chat;
    std::string Text;
    std::vector<MessageEntity> Entites;
};

struct BotLogin {
    bool OK{false};
    User Profile;
};

struct BotUpdate {
private:

    void copy_update_data(const BotUpdate& u) {
        UpdateType = u.UpdateType;
        switch(u.UpdateType) {
            case MESSAGE:
                new (&UpdateData.Message) tg::Message{u.UpdateData.Message};
                break;

            default:
                break;
        }
    }

    void move_update_data(BotUpdate&& u) {
        UpdateType = u.UpdateType;
        switch(u.UpdateType) {
            case MESSAGE:
                new (&UpdateData.Message) tg::Message{std::move(u.UpdateData.Message)};
                break;

            default:
                break;
        }
    }

public:
    enum Type {
        MESSAGE = 1,
        EDITED_MESSAGE,
        CHANNEL_POST,
        EDITED_CHANNEL_POST,
        MESSAGE_REACTION,
        MESSAGE_REACTION_COUNT,
        INLINE_QUERY,
        CHOSEN_INLINE_RESULT,
        CALLBACK_QUERY,
        SHOPPING_QUERY,
        PRE_CHECKOUT_QUERY,
        POLL,
        POLL_ANSWER,
        MY_CHAT_NUMBER,
        CHAT_MEMBER,
        CHAT_JOIN_REQUEST,
        CHAT_BOOST,
        REMOVED_CHAT_BOOST
    };

    union Update {
        bool _dummy;
        tg::Message Message;

        Update() : _dummy{false} {}
        ~Update() { }
    };

    long Id;
    int UpdateType;
    Update UpdateData;

    void set_update(tg::Message&& m) {
        assert(UpdateType == 0);
        UpdateType = MESSAGE;
        new (&UpdateData.Message) tg::Message{ std::move(m) };
    }

    BotUpdate()
        : Id { 0 }
        , UpdateType { 0 }
        , UpdateData{ }
    {}
    BotUpdate(const BotUpdate& u)
        : Id{ u.Id }
        , UpdateType{ u.UpdateType }
    { copy_update_data(u); }

    BotUpdate(BotUpdate&& u) noexcept
        : Id{ u.Id }
        , UpdateType{ u.UpdateType }
    { move_update_data(std::move(u)); }

    BotUpdate& operator=(const BotUpdate& u) {
        auto tmp{ u };
        std::swap(*this, tmp);
        return *this;
    }
    BotUpdate& operator=(BotUpdate&& u) noexcept {
        auto tmp{ std::move(u) };
        std::swap(*this, tmp);
        return *this;
    }

    ~BotUpdate() {
        switch(UpdateType)
        {
            case MESSAGE:
                UpdateData.Message.~Message();
                break;

            default:
                break;
        }
    }
};

struct BotGetUpdates {
    bool OK { false };
    std::vector<BotUpdate> Updates;
};

}