# luaredis

lua访问redis接入层,内部封装了hiredis,采用异步模式.

# 主要接口说明

```lua
lredis = require("lredis");

--创建lredis实例,每个实例代表一个连接
redis = lredis.create_agent();

--连接redis,注意connect是异步的,需要设置on_connect回调响应结果
--2000为超时时间,单位毫秒
redis.connect("127.0.0.1", 6379, 2000);

redis.on_connect = function(ok)
    --ok: 连接是否成功
end

redis.on_disconnect = function()
    --连接丢失...
end


redis.command("set", "now", os.time());

--注意command和reply回调是一对一的,用户可以自行在on_replay中维护一个FIFO队列.
redis.on_reply = function(reply)
    --reply可能是单值的string,int,bool(false),或者是以上类型的一个数组
end

--断开连接
redis.disconnect();

--程序需要在主循环中不断的调用update,这里的0表示io超时时间
redis.update(0);
```


