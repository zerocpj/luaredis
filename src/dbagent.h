/*
** repository: https://github.com/trumanzhao/luaredis.git
** trumanzhao, 2017-10-26, trumanzhao@foxmail.com
*/

#pragma once

#include <hiredis/hiredis.h>
#include "lua.hpp"
#include "luna.h"

class dbagent final
{
public:
    dbagent(lua_State* L);
    ~dbagent();
    void connect(const char* addr, int port, int timeout);
    void disconnect();
    void check_connecting(int timeout);
    int update(int timeout);
    int  command(lua_State* L);

    DECLARE_LUA_CLASS(dbagent);

private:
    void on_reply(redisReply* reply);

private:
    lua_State* m_lvm = nullptr;
    redisContext* m_redis = nullptr;
    redisContext* m_connecting = nullptr;
    int64_t m_connecting_end = 0;
};


