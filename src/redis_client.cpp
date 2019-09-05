/*
** repository: https://github.com/trumanzhao/luaredis.git
** trumanzhao, 2017-10-26, trumanzhao@foxmail.com
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <vector>
#include <unistd.h>
#include "tools.h"
#include "socket_helper.h"
#include "redis_client.h"


EXPORT_CLASS_BEGIN(redis_client)
EXPORT_LUA_FUNCTION(connect)
EXPORT_LUA_FUNCTION(disconnect)
EXPORT_LUA_FUNCTION(update)
EXPORT_LUA_FUNCTION(command)
EXPORT_CLASS_END()

redis_client::redis_client(lua_State* L)
{
    m_lvm = L;
}

redis_client::~redis_client()
{
    disconnect();
}

void redis_client::connect(const char* addr, int port, int timeout)
{
    disconnect();

    if (addr == nullptr || port <= 0)
        return;

    m_connecting = (redisContext*)redisConnectNonBlock(addr, port);
    m_connecting_end = get_time_ms() + timeout;
}

void redis_client::disconnect()
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

void redis_client::check_connecting(int timeout)
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

int redis_client::update(int timeout)
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

    while (m_redis != nullptr && !m_redis->err && check_can_recv(m_redis->fd, timeout))
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

int redis_client::command(lua_State* L)
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
    switch (reply->type)
    {
        case REDIS_REPLY_STRING:
        case REDIS_REPLY_STATUS:
        case REDIS_REPLY_ERROR:
        lua_pushlstring(L, reply->str, reply->len);
        break;

        case REDIS_REPLY_NIL:
        lua_pushboolean(L, false);
        break;

        case REDIS_REPLY_INTEGER:
        lua_pushinteger(L, reply->integer);
        break;

        case REDIS_REPLY_ARRAY:
        lua_newtable(L);
        for (int i = 0; i < reply->elements; i++)
        {
            lua_pushinteger(L, i + 1);
            lua_push_reply(L, reply->element[i]);
            lua_settable(L, -3);
        }
        break;

        default:
        // never !
        lua_pushboolean(L, false);
        break;
    }
}

void redis_client::on_reply(redisReply* reply)
{
    lua_guard g(m_lvm);
    if (!lua_get_object_function(m_lvm, this, "on_reply"))
        return;
    lua_push_reply(m_lvm, reply);
    lua_call_function(m_lvm, nullptr, 1, 0);
}

