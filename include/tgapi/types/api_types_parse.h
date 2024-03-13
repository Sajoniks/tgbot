#pragma once

#include "tgapi/types/api_types.h"

#include <functional>
#include <rapidjson/document.h>

namespace tg::parse {

#pragma region Parse Interface

template<typename Parse>
struct ParseTag {};

template<typename To, typename T>
auto do_parse(const T& t) {
    return do_parse(t, ParseTag<To>{});
}

template<typename To, typename T, typename... Args>
auto do_parse(const T& t, Args&&... args) {
    return do_parse(t, ParseTag<To>{}, std::forward<Args>(args)...);
}

#pragma endregion // Parse Interface

#pragma region Details

namespace detail {
    template<typename Object, typename Proj>
    void map_json_value(const Object& o, std::string_view key, Proj&& p) {
        auto it = o.FindMember(key.data());
        if (it != std::end(o)) {
            std::invoke(p, it->value);
        }
    }
}

#pragma endregion // Details


template<typename To>
auto do_parse(const JArray& arr, ParseTag<std::vector<To>>) {
    std::vector<To> result;
    for (const auto& e : arr) {
        auto value = tg::parse::do_parse<To>(e.GetObj());
        result.push_back(std::move(value));
    }
    return result;
}


template<typename To>
auto do_parse(const JObj& d, ParseTag<Result<To>>) {
    if (d["ok"].GetBool()) {
        return Result<To>::from_content( do_parse<To>(d["result"].GetObj()) );
    } else {
        return Result<To>::from_error( d["description"].GetString() );
    }
}


inline auto do_parse(const ChatId& id, ParseTag<JValue>, JAlloc& a) {
    switch(id.index()) {
        case 0:
            return JValue{ std::get<std::string>(id).c_str(), a };
        case 1:
            return JValue{  std::get<long>(id) };
        default:
            throw std::runtime_error("invalid chat id");
    }
}


inline auto do_parse(const JObj& d, ParseTag<ReplyParameters>) {
    tg::ReplyParameters p;

    detail::map_json_value(d, "message_id", [&p](const JValue& v) { p.MessageId = v.GetInt64(); });

    return p;
}
inline auto do_parse(const ReplyParameters& p, ParseTag<JValue>, JAlloc& a) {
    JValue o { rapidjson::kObjectType };
    {
        o.AddMember("message_id", p.MessageId, a);
    }
    return o;
}


inline auto do_parse(const JObj& d, ParseTag<tg::Chat>) {
    tg::Chat c;

    detail::map_json_value(d, "id", [&c](const JValue& v) {
        if (v.IsInt64()) {
            c.Id = v.GetInt64();
        } else if (v.IsString()) {
            c.Id = v.GetString();
        }
    });

    return c;
}


inline auto do_parse(const JObj& d, ParseTag<tg::User>) {
    tg::User pf;

    detail::map_json_value(d, "id", [&pf](const JValue& v) { pf.Id = v.GetInt64(); });
    detail::map_json_value(d, "first_name", [&pf](const JValue& v) { pf.FirstName = v.GetString(); });
    detail::map_json_value(d, "last_name", [&pf](const JValue& v) { pf.LastName = v.GetString(); });
    detail::map_json_value(d, "username", [&pf](const JValue& v) { pf.UserName = v.GetString(); });
    detail::map_json_value(d, "language_code", [&pf](const JValue& v) { pf.LanguageTag = v.GetString(); });

    detail::map_json_value(d, "is_bot", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::BOT;
        }
    });
    detail::map_json_value(d, "can_join_groups", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::CAN_JOIN_GROUPS;
        }
    });
    detail::map_json_value(d, "can_read_all_group_messages", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::CAN_READ_ALL_GROUP_MESSAGES;
        }
    });
    detail::map_json_value(d, "supports_inline_queries", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::SUPPORTS_INLINE_QUERIES;
        }
    });
    detail::map_json_value(d, "is_premium", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::PREMIUM;
        }
    });
    detail::map_json_value(d, "added_to_attachment_menu", [&pf](const JValue& v) {
        if (v.GetBool()) {
            pf.Flags |= tg::UserFlags::ADDED_TO_ATTACHMENT_MENU;
        }
    });

    return pf;
}


inline auto do_parse(const JObj& d, ParseTag<tg::BotLogin>) {
    tg::BotLogin l;

    detail::map_json_value(d, "ok",[&l](const JValue& v) { l.OK = v.GetBool(); });
    detail::map_json_value(d, "result",[&l](const JValue& v) { l.Profile = do_parse<tg::User>(v.GetObj()); });

    return l;
}


inline auto do_parse(const JObj& d, ParseTag<tg::MessageEntity>) {
    tg::MessageEntity e;

    static const std::unordered_map<std::string, std::uint32_t> TYPE_STR_TO_ENUM {
            { "bot_command", MessageEntity::BOT_COMMAND}
    };

    detail::map_json_value(d, "type", [&e](const JValue& v) {
        const std::string type = v.GetString();
        e.Type = TYPE_STR_TO_ENUM.at(type);
    });

    detail::map_json_value(d, "offset", [&e](const JValue& v) { e.Offset = v.GetUint(); });
    detail::map_json_value(d, "length", [&e](const JValue& v) { e.Length = v.GetUint(); });

    return e;
}


inline auto do_parse(const JObj& d, ParseTag<tg::Message>) {
    tg::Message m;

    detail::map_json_value(d, "message_id", [&m](const JValue& v) { m.Id = v.GetInt64(); });
    detail::map_json_value(d, "chat", [&m](const JValue& v) { m.Chat = parse::do_parse<tg::Chat>(v.GetObj()); });
    detail::map_json_value(d, "from", [&m](const JValue& v) { m.From = parse::do_parse<tg::User>(v.GetObj()); });

    detail::map_json_value(d, "text", [&m](const JValue& v) { m.Text = v.GetString(); });
    detail::map_json_value(d, "entities", [&m](const JValue& v) { m.Entites = parse::do_parse<std::vector<tg::MessageEntity>>(v.GetArray()); });

    return m;
}


inline auto do_parse(const JObj& d, ParseTag<tg::BotUpdate>) {
    BotUpdate u;

    detail::map_json_value(d, "update_id", [&u](const JValue& v) { u.Id = v.GetInt64(); });

    if (d.HasMember("message")) {
        auto m = parse::do_parse<tg::Message>(d["message"].GetObj());
        u.set_update(std::move(m));
    }

    return u;
}


inline auto do_parse(const JObj& d, ParseTag<tg::BotGetUpdates>) {
    tg::BotGetUpdates g;
    detail::map_json_value(d, "ok", [&g](const JValue& v) { g.OK = v.GetBool(); });
    detail::map_json_value(d, "result", [&g](const JValue& v) { g.Updates = parse::do_parse<std::vector<BotUpdate>>(v.GetArray()); });
    return g;
}

}