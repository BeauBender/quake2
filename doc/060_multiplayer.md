# Multiplayer Servers

In general running a Yamagi Quake II server is the same as running a
Vanilla Quake2 server, so the old guides should still apply.

One thing to keep in mind is that the server must be restarted at least
every 49 days, because the Quake II network protocol represents the
interal time as a 32 bit integer and after 49 days that integer
overflows, leading to all kinds of trouble.

This problem has always existed in Quake II and is not fixable (at least
not without breaking compatibility with the existing network protocol),
but back in Win9x days this was less of a problem because Windows
crashed frequently anyways and Win9x had the same bug and crashed after
49 days or so...

Apart from this, we'll only document changes/additions here.



## Security considerations

Quake II was released in 1997 into a world where security didn't matter.
The most important thing when connecting to random servers is: **Quake
II allows the server to do anything on the client!** The server may
execute any console command, it may overwrite any cvar, download any
file to your computer and given the rather fragile state of the command
parsers chances are high that there are remote code execution
vulnerabilities. Only connect to trustworthy servers!



## Map rotation

Map rotations are configured through the `sv_maplist` CVar added with
Quake II patch 3.15. The map list must be enclosed in quotation marks
and all maps must exist. Start the game with the first map.

For example: `q2ded +set sv_maplist '"q2dm1 q2dm2 q2dm3"' +map q2dm1`
