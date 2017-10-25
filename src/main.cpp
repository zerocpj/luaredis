#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "socket_helper.h"
#include "dbagent.h"

int create_agent(lua_State* L)
{
    lua_push_object(L, new dbagent(L));
    return 1;
}

extern "C" int luaopen_lredis(lua_State* L)
{
    lua_newtable(L);
    lua_set_table_function(L, -1, "create_agent", create_agent);
    return 1;
}


