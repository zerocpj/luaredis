#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>
#include <unistd.h>
#include "tools.h"
#include "socket_helper.h"
#include "dbagent.h"


EXPORT_CLASS_BEGIN(dbagent)
EXPORT_LUA_FUNCTION(connect)
EXPORT_LUA_FUNCTION(disconnect)
EXPORT_LUA_FUNCTION(update)
EXPORT_LUA_FUNCTION(command)
EXPORT_CLASS_END()

dbagent::dbagent(lua_State* L)
{
    m_lvm = L;
}

dbagent::~dbagent()
{
    disconnect();
}

void dbagent::connect(const char* addr, int port, int timeout)
{
    disconnect();

    if (addr == nullptr || port <= 0)
        return;

    m_connecting = (redisContext*)redisConnectNonBlock(addr, port);
    m_connecting_end = get_time_ms() + timeout;
}

void dbagent::disconnect()
{
    if (m_connecting != nullptr)
    {
        redisFree(m_connecting);
        m_connecting = nullptr;
    }

    if (m_redis != nullptr)
    {
        redisFree(m_redis);
        m_redis = nullptr;
    }
}

void dbagent::check_connecting(int timeout)
{
    int64_t now = get_time_ms();
    if (now > m_connecting_end)
    {
        disconnect();

        lua_guard g(m_lvm);
        lua_call_object_function(m_lvm, nullptr, this, "on_connect", std::tie(), false);
        return;
    }

    if (!check_can_send(m_connecting->fd, timeout))
        return;

    int err = 0;
    socklen_t sock_len = sizeof(err);
    int ret = getsockopt(m_connecting->fd, SOL_SOCKET, SO_ERROR, (char*)&err, &sock_len);
    if (ret == 0 && err == 0)
    {
        m_redis = m_connecting;
        m_connecting = nullptr;

        lua_guard g(m_lvm);
        lua_call_object_function(m_lvm, nullptr, this, "on_connect", std::tie(), true);
    }
    else
    {
        disconnect();

        lua_guard g(m_lvm);
        lua_call_object_function(m_lvm, nullptr, this, "on_connect", std::tie(), false);
    }
}

int dbagent::update(int timeout)
{
    if (m_connecting)
    {
        check_connecting(timeout);
        return 0;
    }

    if (m_redis != nullptr)
    {
        int done = 0;
        while (done == 0 && check_can_send(m_redis->fd, timeout))
        {
            redisBufferWrite(m_redis, &done);
        }
    }

    int count = 0;

    while (m_redis != nullptr && check_can_recv(m_redis->fd, timeout))
    {
        redisBufferRead(m_redis);

        while (m_redis != nullptr)
        {
            redisReply* reply = nullptr;
            redisGetReply(m_redis, (void**)&reply);
            if (reply == nullptr)
                break;

            on_reply(reply);
            freeReplyObject(reply);
            count++;
        }
    }

    if (m_redis != nullptr && m_redis->err)
    {
        disconnect();

        lua_guard g(m_lvm);
        lua_call_object_function(m_lvm, nullptr, this, "on_disconnect");
    }

    return count;
}

int dbagent::command(lua_State* L)
{
    int top = lua_gettop(L);
    if (top == 0 || m_redis == nullptr)
        return 0;

    std::vector<const char*> args;
    std::vector<size_t> lens;

    for (int i = 1; i <= top; i++)
    {
        size_t len = 0;
        const char* arg = lua_tolstring(L, i, &len);
        if (arg == nullptr || len == 0)
            return 0;
        args.push_back(arg);
        lens.push_back(len);
    }

    int ret = redisAppendCommandArgv(m_redis, top, &args[0], &lens[0]);
    if (ret != REDIS_OK)
        return 0;

    lua_pushboolean(L, true);
    return 1;
}

static void lua_push_reply(lua_State* L, redisReply* reply)
{
    lua_newtable(L);

    switch (reply->type)
    {
        case REDIS_REPLY_STRING:
        lua_pushstring(L, "type");
        lua_pushstring(L, "string");
        lua_settable(L, -3);

        lua_pushstring(L, "str");
        lua_pushlstring(L, reply->str, reply->len);
        lua_settable(L, -3);
        break;

        case REDIS_REPLY_STATUS:
        lua_pushstring(L, "type");
        lua_pushstring(L, "status");
        lua_settable(L, -3);

        lua_pushstring(L, "str");
        lua_pushlstring(L, reply->str, reply->len);
        lua_settable(L, -3);
        break;

        case REDIS_REPLY_ERROR:
        lua_pushstring(L, "type");
        lua_pushstring(L, "error");
        lua_settable(L, -3);

        lua_pushstring(L, "str");
        lua_pushlstring(L, reply->str, reply->len);
        lua_settable(L, -3);
        break;

        case REDIS_REPLY_NIL:
        lua_pushstring(L, "type");
        lua_pushstring(L, "nil");
        lua_settable(L, -3);
        break;

        case REDIS_REPLY_INTEGER:
        lua_pushstring(L, "type");
        lua_pushstring(L, "integer");
        lua_settable(L, -3);

        lua_pushstring(L, "integer");
        lua_pushinteger(L, reply->integer);
        lua_settable(L, -3);
        break;

        case REDIS_REPLY_ARRAY:
        lua_pushstring(L, "elements");
        lua_newtable(L);
        for (int i = 0; i < reply->elements; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_push_reply(L, reply->element[i]);
            lua_settable(L, -3);
        }
        lua_settable(L, -3);
        break;

        default:
        lua_pushstring(L, "type");
        lua_pushinteger(L, reply->type);
        lua_settable(L, -3);
        break;
    }
}

void dbagent::on_reply(redisReply* reply)
{
    lua_guard g(m_lvm);
    if (!lua_get_object_function(m_lvm, this, "on_reply"))
        return;
    lua_push_reply(m_lvm, reply);
    lua_call_function(m_lvm, nullptr, 1, 0);
}
