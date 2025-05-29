# fslatency

A filesystem latency measurement and alarm tool, implemented in client-server architecture.

forked from https://github.com/maulisadam/perfmeters

## How to use it and why?

If you are investigating errors in a very complex system that indicate random slowdowns or loss of disk storage, install this tool on as many VMs as possible and monitor the server output. The server must handle thousands of clients without significant CPU usage.

You can also send a summary of the data to Grafana to better understand your system.

## The Goal

The sooner, the more accurately, the better to detect when a disk system is stuck.

- More accurately: Minimizing false cases is the primary goal.
- Soon: Show within 5 seconds if there is a problem.

All this is done through the extremely complicated shared storage - FC - ESX - VM environment.

## Architecture

The system consists of two parts. A monitoring agent (installed on many VMs) and a (single) data processor.
The requirement is that the system can still work for a short time even in the event of a disk/disk system failure.
For this reason, the alerts (issued by the data processor itself) cannot use disk. Technically, it can only be stdout.
Similarly, only diskless communication can be used between the monitoring agent and the data processor. Technically, this means UDP.
The monitoring agent should only depend on the disk it is currently measuring. There should be no config reading on the fly or any other open().

Because of this, the monitoring agent is a statically linked monolithic program written in C.
The data processor could be different. However, the data processor must run in a place where the operating system is not dependent on the monitored disk system. This can be just a small target computer.
Which also means that it must be a statically linked monolithic program.
The name resolution cannot be translated to static (libc-dependent) so there is no name resolution: an IP address must be specified.

### The monitoring agent

    fslatency --serverip a.b.c.d [--serverport PORT] [--text "FOO"] --file /var/lib/fslatency/check.txt
    [--nocheckfs] [--nomemlock] [--debug] [--version]

Where:

- a.b.c.d  The IP address of the data processor. It sends UDP packets here. No default
- PORT  The UDP port number of the data processor. Default: 57005 (0xDEAD)
- "FOO" a freetext field, which is sent in the UDP packets. This is optional. This makes it possible to distinguish between two monitoring agents running on a same VM (if we want to monitor two disks, for example). Max 63 characters.
- file A specific filename that exists on a real filesystem on a real blockdevice. So NOT tmpfs, NOT nfs and NOT fuse. This file is regularly written/written, deleted, created. This is how the measurement is done.
- --nocheckfs It does not check whether the given file exists on the local filesystem. Do not use it.
- --nomemlock Does not lock the process pages in memory. Default: locks them.
- --debug
- --version

#### Architecture

Two pthreads, one of which measures the overall response time of the filesystem/disk system by continuously writing to the file.
The other thread monitors this and periodically (every second) sends a short report of this to the data processor.

It only writes to syslog/stdout at startup, and if it gets a valid filesystem error (disk full, no permissions, etc.). In the event of a crash, stuck, etc., it doesn't even try to write locally.


### data processor

    Usage: fslatency_server [--bind a.b.c.d] [--port PORT] [--maxclient 509]
       [--timetoforget 600] [--udptimeout 3] [--alarmstatusperiod 1]
       [--statusperiod 300] [--alarmtimeout 8] [--latencythresholdfactor 15.0]
       [--rollingwindow 60] [--minimummeasurementcount 60]
       [--graphitebase metric.path.base --graphiteip 1.2.3.4 [--graphiteport 2003]]
       [--nomemlock] [--debug[=1]] [--version]


Where:

- --bind a.b.c.d the IP address of the interface to listen. Default: 0.0.0.0 (all)
- --port PORT The address of the UDP port it is listening on. Default: 57005 (0xDEAD)
- --maxclient Integer. The size of the internal client table. The program is NOT dynamic, this is allocated at startup. It cannot handle more clients than this. Default: 509 (a nice prime)
- --timetoforget Integer, seconds. How long to forget a client that is not sending data. Default: 600 (10 minutes, not prime, but at least round)
- --udptimeout Integer, seconds. How long should a client be considered lost (alarm event)?
- --alarmstatusperiod Integer, seconds. If there is an alarm, how often should the status be printed. Default 1 sec. Not an exact value.
- --statusperiod Integer, seconds. If there is no alarm, then it should print status periodically. Default 300 (5 minutes). Not an exact value.
- --alarmtimeout Integer, Seconds. How long it takes to forget the alarm (if there was no new one). Default 8. This prevents alarm flooding in the case of flipflop.
- --latencythresholdfactor float. If the latency reported by the client deviates from the average of the previous ones by more than this many times the standard deviation, then it will raise an alarm. Default: 15. This is a bit mathematical. The point is that if you raise this threshold, the number of false alarms will decrease. This is not a normal distribution, 3 will be too small.
- --rollingwindow Integer, seconds/piece. This is the maximum number of packets of data to generate a statistical alarm. Default: 60. This means that it will alert based on the characteristics of the previous 1 minute, if necessary.
- --minimummeasurementcount Integer, pieces. There must be at least this many measurements for the statistical alarm to sound. Default: 60 measurements (approx. 5-6 sec)

- --graphitebase String. Optional. If specified, it will act as a gateway and send the data to a graphite server, giving an output in the form of graphite(carbon) plaintext input.
- --graphiteip 1.2.3.4 Optional. is the IP address of the graphite server (no default). Only taken into account if --graphitebase is not zero.
- --graphiteport 2003. The tcp port for the graphite server's plaintext input. Default: 2003.
- --nomemlock Does not lock the process pages in memory. Default: locks them.
- --debug some global debug info, no flood
- --debug=2 additional debug for each packet
- --debug=3 additional debug for packet's data


The data processor automatically adds the agent to the list of agents to be monitored if it receives a UDP packet from it.
And it also alerts you immediately if it does not receive any more from it. The alert does not come out more than once, it only appears in the regular status report.

However, if it does not receive any packets from it for a "timetoforget" period (10 minutes), it is removed from the list of agents to be monitored and no more alerts are issued.

Indications (All to the stdout)

- There is a new agent
- Agent is removed due to "timetoforget"

Non-alarm events that this handles but does not indicate:

- A packet was dropped. (The udptimeout did not expire, but a packet was dropped and the next packet replaced it) "packet loss". Protocol 7 tolerates such loss.

Alarm events that this handles and also indicate:

Only the very first one comes out, then the data processor goes into "alarm" state.

- agent did not send a packet (udptimeout has elapsed since the last packet) -> "agent lost"
- agent sent a packet, but it has 0 measurements (could not measure) -> "stuck"
- agent sent a measurement, however it is unrealistic (far exceeds the value expected from previous ones) -> "bad latency" LOW and HIGH separately.

Státusz kiírások

- In alarm state, it prints status every second.
- In non-alarm state, it prints every 5 minutes.
- Status display: timestamp, number of agents, number of "problem" agents (details: agent lost, not measuring, bad latency, communication error), latency min/max/mean/std

Latency values are in msec, but their logarithm is listed everywhere (natural base logarithm)!


### UDP communication internals and data structures

We don't do host-to-network (endianess) transformation, so monitoring agent and data processor must be running same architecture.


- "fslatency      \0" fix string  (16 byte)
- protocol version 32 bit (16 bit major, 16 bit minor);
- hostname (64 karakter, '\0' filled)
- text (64 karakter '\0' filled) See a monitoring agent --text options
- measuring precision struct timespec == 64 bit
- last 1 sec datablock
- 2nd previous 1sec datablock
- 3rd previous 1sec datablock
- 4th previous 1sec datablock
- 5th previous 1sec datablock
- 6th previous 1sec datablock
- 7th previous 1sec datablock
- 8th previous 1sec datablock

Datablock:

- number of measurements in this datablock (integer, 64 bit)
- starttime  struct timespec == 64 bit (unix time: from UTC epoch)
- endtime  struct timespec == 64 bit
- min (float 64bit)
- max (float 64bit)
- sumX (float 64bit) sum of all measurements in this interval.
    Can be used to calculate the average
- sumXX (float 64bit) sum of squares of all measurements in this interval.
    Can be used to calculate std deviation

### A little math

We calculate the mean and standard deviation so that we can automatically determine whether an outlier is "still within" or not.
I didn't want to use fixed thresholds, because the concept of "still within" means different things for different disk systems.
Unfortunately, I don't know the latency distribution. I've done a couple of measurements, and it seems that it's very, very not normally distributed.
Outliers with very high latency values occur once or twice as a matter of course. And the whole thing had an exponential characteristic.

Exponential distribution? No, because the density function definitely has a ramp.
Lognorm? Not quite. It decays more slowly, but starts faster. It has more of a k=2 Chi-squared distribution, but it decays more slowly.

- For these reasons, I first change the resolution of the measurement data: I change the resolution from nanosec to millisec. This value is the average of the magnitude of a modern SSD filesystem operation.
- I take the logarithm of the resulting value. Although the distribution is still far from normal (it has a very long tail), it is now manageable.
- I didn't want to cut off the few pieces of data that were very outliers in the name of data cleaning. I'm specifically curious about the single outlier data that suddenly appears. Not just the rising average, but specifically the single outlier as well.
- After this, the program included the stddev after the logarithm, and the calculation with 15 times the stddev.

The whole thing is topped off with an alert when an empty packet arrives (stuck). This automatically adds an upper threshold to the alert: If the latency exceeds 1 second, it definitely alerts.
