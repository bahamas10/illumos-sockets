opensockets(1)
==============

A substitute for `pfiles(1)` on [Illumos][0] that does as
little as possible to list open ports on a machine in the quickest and
least invasive way possible

Warning
-------

Not fully tested, could contain bugs, could break everything... use at your
own risk.

Also, information not guaranteed to be accurate - race conditions, etc.  This
is meant to give a quick 1000 foot view and provide a simple answer to "what
ports are open on my machine?"

Usage
-----

    $ make
    cc -Wall -lproc -lsocket -lnsl opensockets.c -o opensockets
    $ ./opensockets -h
    usage: opensockets [-h] [-v] [-H] [[pid] ...]

    print all ports in use on the current system

    options
      -d       turn on debug output
      -h       print this message and exit
      -H       don't print header

Example
-------

IPs and ports changed for privacy

    $ sudo ./opensockets
    PID      EXEC         IP                PORT    ARGS
    18250    node         127.0.0.1         10501   node /opt/local/bin/http-host-proxy
    16086    master       127.0.0.1         25      /opt/local/libexec/postfix/master
    41074    bud          0.0.0.0           443     /opt/local/bin/bud -c /opt/local/etc/bud/bud.json
    18905    nrpe         10.0.0.1          5666    nrpe -n -c /opt/local/etc/nagios/nrpe.cfg -d
    18901    sshd         0.0.0.0           22      /usr/lib/ssh/sshd
    18362    redis-server 10.0.0.1          6411    /opt/local/bin/redis-server /opt/local/etc/redis/redis.conf
    18637    beam.smp     127.0.0.1         8098    /opt/local/lib/riak/erts-5.9.1/bin/beam.smp -K true -A 16 -W w -P 131072 -zdbbl

About
-----

This program uses `libproc` on Illumos, but opts to do a lot of the work itself -
like listing pids, listing fds, etc.  The reasoning behind this is:

1. do as little as possible to list listening TCP sockets on the machine - don't process regular files, pipes, etc.
2. older platforms don't have https://www.illumos.org/issues/5397, so `libproc` can be needlessly slow for sockets

When run with no arguments, it simply loops all pids in `/proc/*`, and then
loops every fd for every pid in `/proc/<pid>/fd/*`.  When it encounters (using
`stat(2)`) a socket file, it uses `Pgrab` to grab the process, and does the
work required to ensure it is a TCP IPv4 socket that is listening on a specific
port for incoming requests.  Once all that is done a line is printed with
process information and the IP and port.

Performance
-----------

### `lsof`

Using `lsof` (which uses `pfiles(1)` under the hood) from the `smtools` package on:

A platform that does NOT include the https://www.illumos.org/issues/5397 patch

```
# ptime lsof -p >/dev/null

real     1:21.624016149
user        2.027642428
sys      1:19.689849204
```

A platform that does include the https://www.illumos.org/issues/5397 patch

```
# ptime lsof -p >/dev/null

real       11.418651896
user        2.204807567
sys         8.970867687
```

### `opensockets`

Using this program

```
# ptime ./opensockets >/dev/null

real        0.382610819
user        0.046179765
sys         0.241209266
```

### results

In the output above, the `real` time will show the actual time elapsed during
the programs execution.

- `lsof` - without patch - 1 minute 21 seconds
- `lsof` - with patch - 11.4 seconds
- `opensockets` - 0.38 seconds

First, just looking at `lsof`, the patch provides an incredible boost in speed
when running `pfiles(1)`, so `lsof` sees the benefits of it - it allows it to
return the same amount of information **13.5x** faster than before!

The good news is, this [patch has landed][2] in the [20150306T202346Z][3]
release of [SmartOS][1], so as time goes on it will become the new
normal for SmartOS and Illumos users.

However, when comparing this to `opensockets`, `opensockets` is **30x** faster
than even the fastest `lsof`!  Just for fun, that is **213x** faster than the
slowest `lsof`.

Finally, it's important to remember, that the processes being interrogated by
`pfiles(1)` and `opensockets` are stopped as a result of this process.  So
speeding this process up has the added side-effect of reducing the problems
that can arise when running this on a production system with thousands
of open files.

License
-------

MIT License

[0]: http://illumos.org
[1]: http://smartos.org
[2]: https://github.com/illumos/illumos-gate/commit/d907f8b938aec9d8b57fdb15c241b98641b8b052
[3]: https://us-east.manta.joyent.com/Joyent_Dev/public/SmartOS/20150306T202346Z/index.html
