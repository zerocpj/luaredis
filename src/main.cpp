/*
** repository: https://github.com/trumanzhao/luaredis.git
** trumanzhao, 2017-10-26, trumanzhao@foxmail.com
*/

#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "socket_helper.h"
#include "redis_client.h"

int create_redis_client(lua_State* L)
{
    lua_push_object(L, new redis_client(L));
    return 1;
}

extern "C" int luaopen_lredis(lua_State* L)
{
    lua_newtable(L);
    lua_set_table_function(L, -1, "create_redis_client", create_redis_client);
    return 1;
}


