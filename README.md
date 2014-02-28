# zmq-tools: ZeroMQ tools

## zmq-server

ZeroMQ Server.

```
Usage: zmq-server -e <ENDPOINT> -t <TYPE> [-s SUBSCRIBE]
                  [-S <SCRIPT> [-d <DELIMITER>]]
                  [-D <COMMAND> [-p <FILE>]] [-l <FILE>]
```

  option                    | description
 ---------------------------|-------------
  -e, endpoint=ENDPOINT     | ZeroMQ socket endpoint
  -t, --type=TYPE           |    ZeroMQ type [PULL|SUB|REP|STREAM]
  -s, --subscribe=SUBSCRIBE | ZeroMQ subscribe key [type=SUB] <default: none>
  -S, --script=SCRIPT       | script to run after receive
  -d, --delimiter=DELIMITER | delimiter when sending the script <default: none>
  -D, --daemon=COMMAND      | daemon command [start|stop]
  -p, --pidfile=FILE        | pid file <default: /var/run/zmqd.pid>
  -l, --log=FILE            | log file <default: none(stderr)>

## zmq-client

ZeroMQ Client.

```
Usage: zmq-client -e <ENDPOINT> -t <TYPE> [-l <FILE>] MESSAGE ...
```

  option                 | description
 ------------------------|-------------
 -e, --endpoint=ENDPOINT | ZeroMQ socket endpoint
 -t, --type=TYPE         | ZeroMQ type [PUSH|PUB|REQ|STREAM]
 -l, --log=FILE          | log file <default: none(stderr)>

## Examples

Server side process.

```
% cat > echo.sh <<EOF
#!/bin/sh
if [ -p /dev/stdin ]; then
    cat -
fi
echo
exit 0
EOF
% chmod 755 echo.sh
% zmq-server -e tcp://127.0.0.1:5555 -t pull -S ./echo.sh
```

Client side process.

send to `test` message.

```
% zmq-client -e tcp://127.0.0.1:5555 -t push test
```
