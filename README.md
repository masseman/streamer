Stream raw PCM from a Windows recording device to a Linux machine over a TCP/IP socket.

Windows side
============

Compile Streamer using Visual C++ and install as a service by doing
```
sc create "Network Sound Streamer" binpath= "<your install directory>\Streamer.exe /D <name of device> /H <host> /P <port>"
```
or run in command-line window by doing
```
Streamer.exe /N /D <name of device> /H <host> /P <port>
```

In order to stream all sound being played, the device to listen to should ideally "Stereo Mix". However, since it is not always available on newer versions of Windows, it may be necessary to install [Virtual Audio Cable](http://software.muzychenko.net/eng/vac.htm) or similar.

Linux side
==========

Simply run `netcat` and pipe into `aplay`:
```
while true; do netcat -l -p 1234 | aplay -f S16_LE -r 44100 -c 2 --disable-resample --disable-format --disable-channels --nonblock --buffer-time=2000
```

The program `tcpfwd` in the `tcpfwd` directory adds a timeout functionality, since `netcat` does not always seem to notice when the socket is closed. Running `tcpfwd` with a five-second timeout would look like this:
```
while true; do tcpfwd 1234 5 | aplay -f S16_LE -r 44100 -c 2 --disable-resample --disable-format --disable-channels --nonblock --buffer-time=2000
```

The while loop can be used to add commands to kill processes holding up the audio device, if that is an issue.
