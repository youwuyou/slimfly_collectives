# Benchmarks


## Guideline: from openmpi.org FAQ 

>Q: I want to run some performance benchmarks with Open MPI.>How do I do that?


Running benchmarks is an extremely difficult task to do correctly. There are many, many factors to take into account; it is not as simple as just compiling and running a stock benchmark application. This FAQ entry is by no means a definitive guide, but it does try to offer some suggestions for generating accurate, meaningful benchmarks.

Decide exactly what you are benchmarking and setup your system accordingly. For example, if you are trying to benchmark maximum performance, then many of the suggestions listed below are extremely relevant (be the only user on the systems and network in question, be the only software running, use processor affinity, etc.). If you're trying to benchmark average performance, some of the suggestions below may be less relevant. Regardless, it is critical to know exactly what you're trying to benchmark, and know (not guess) both your system and the benchmark application itself well enough to understand what the results mean.
To be specific, many benchmark applications are not well understood for exactly what they are testing. There have been many cases where users run a given benchmark application and wrongfully conclude that their system's performance is bad — solely on the basis of a single benchmark that they did not understand. Read the documentation of the benchmark carefully, and possibly even look into the code itself to see exactly what it is testing.

Case in point: not all ping-pong benchmarks are created equal. Most users assume that a ping-pong benchmark is a ping-pong benchmark is a ping-pong benchmark. But this is not true; the common ping-pong benchmarks tend to test subtly different things (e.g., NetPIPE, TCP bench, IMB, OSU, etc.). *Make sure you understand what your benchmark is actually testing.*

Make sure that you are the only user on the systems where you are running the benchmark to eliminate contention from other processes.
Make sure that you are the only user on the entire network / interconnect to eliminate network traffic contention from other processes. This is usually somewhat difficult to do, especially in larger, shared systems. But your most accurate, repeatable results will be achieved when you are the only user on the entire network.
Disable all services and daemons that are not being used. Even "harmless" daemons consume system resources (such as RAM) and cause "jitter" by occasionally waking up, consuming CPU cycles, reading or writing to disk, etc. The optimum benchmark system has an absolute minimum number of system services running.
Use processor affinity on multi-processor/core machines to disallow the operating system from swapping MPI processes between processors (and causing unnecessary cache thrashing, for example).
On NUMA architectures, having the processes getting bumped from one socket to another is more expensive in terms of cache locality (with all of the cache coherency overhead that comes with the lack of it) than in terms of hypertransport routing (see below).

Non-NUMA architectures such as Intel Woodcrest have a flat access time to the South Bridge, but cache locality is still important so CPU affinity is always a good thing to do.

Be sure to understand your system's architecture, particularly with respect to the memory, disk, and network characteristics, and test accordingly. For example, on NUMA architectures, most common being Opteron, the South Bridge is connected through a hypertransport link to one CPU on one socket. Which socket depends on the motherboard, but it should be described in the motherboard documentation (it's not always socket 0!). If a process on the other socket needs to write something to a NIC on a PCIE bus behind the South Bridge, it needs to first hop through the first socket. On modern machines (circa late 2006), this hop cost usually something like 100ns (i.e., 0.1 us). If the socket is further away, like in a 4- or 8-socket configuration, there could potentially be more hops, leading to more latency.
Compile your benchmark with the appropriate compiler optimization flags. With some MPI implementations, the compiler wrappers (like mpicc, mpif90, etc.) add optimization flags automatically. Open MPI does not. Add -O or other flags explicitly.
Make sure your benchmark runs for a sufficient amount of time. Short-running benchmarks are generally less accurate because they take fewer samples; longer-running jobs tend to take more samples.
If your benchmark is trying to benchmark extremely short events (such as the time required for a single ping-pong of messages):
Perform some "warmup" events first. Many MPI implementations (including Open MPI) — and other subsystems upon which the MPI uses — may use "lazy" semantics to setup and maintain streams of communications. Hence, the first event (or first few events) may well take significantly longer than subsequent events.
Use a high-resolution timer if possible — gettimeofday() only returns millisecond precision (sometimes on the order of several microseconds).
Run the event many, many times (hundreds or thousands, depending on the event and the time it takes). Not only does this provide more samples, it may also be necessary, especially when the precision of the timer you're using may be several orders of magnitude less precise than the event you're trying to benchmark.
Decide whether you are reporting minimum, average, or maximum numbers, and have good reasons why.
Accurately label and report all results. Reproducibility is a major goal of benchmarking; benchmark results are effectively useless if they are not precisely labeled as to exactly what they are reporting. Keep a log and detailed notes about the exact system configuration that you are benchmarking. Note, for example, all hardware and software characteristics (to include hardware, firmware, and software versions as appropriate).

