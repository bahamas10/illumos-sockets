opensockets(1)
==============

A substitute for `pfiles(1)` on [Illumos](http://illumos.org) that does as
little as possible to list open ports on a machine in record speed

Warning
-------

Not fully tested, could contain bugs, could break everything... use at your
own risk.


Also, information not guaranteed to be accurate - race conditions, etc.  This
is meant to give a quick 1000 foot view.

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
`stat(2)`) a socket file, it uses `Pgrab` to grab the process, and does the work required
to ensure it is a TCP IPv4 socket that is listening on a specific port for incoming
requests.  Once all that is done a line is printed with process information and the IP and port.

Why?
----

Speed.

Using `lsof` (which uses `pfiles(1)` under the hood) from the `smtools`
package it takes 1 minute and 15 seconds to list all ports (some duplicates) on
my test machine.  Using `opensockets` it takes 1.5 seconds on the same machine!

License
-------

MIT License
