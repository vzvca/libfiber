# libfiber

Userland cooperative threads aka fibers aka green threads aka coroutines.

## Introduction

This small librairy implements user land cooperative threads, they are called fibers. Fibers can be seen as a specialized case of coroutines which are a wider concept.

Each fiber has its own stack and shares the CPU with other fibers.
When it resumes execution, it restarts where it was just like a normal thread.
The difference with a `normal' thread is that fibers to not run concurrently but sequentially in the same `kernel' thread.

It has advantages :
 * there is no need to lock data used by several fibers because they access this data sequentially
 * they are more lighter than `kernel' thread and context switch is very fast

And it has drawbacks too :
 * each fiber must be `fair' with other fibers and yield the CPU to give others a chance to run. If a single fiber fails to do so, the whole program will hang.
 * fibers will not take advantage of multi-processor machines, they will not spread naturally across the available CPU cores.


In this libray, a fiber is associated to a scheduler. A scheduler will contain many fibers. A scheduler can run by itself at a periodic rate or can be triggered manually.

### Compilation

This library has been tested on linux debian on amd64, armhf and ppc64 architectures and on Cygwin. A slightly modified version has been used in production code.


### Unit tests

Unit tests depends on the check framework, which on debian is installed with :

~~~~
sudo apt-get install check
~~~~

To run them, type 'make check'. The unit tests are interesting to see how to use the lirary.


### Demos

A few demonstrations are provided :
 * perf : this is the most simple example. It will create 100 fibers which all do the same, they increment a global counter. The program stops when the counter reaches 50000000, which corresponds to 50000000 context switch between fibers. It then prints the time needed and the number of context switches.
 * basic : in this example, three fibers are created. They all do a small task, the scheduler run 3 times ans stops.
 * http: a small http server is started. A new fiber is started each time a new connection is done. The server is single threaded but the fibers share the CPU and handles several long lasting requests (like video delivery) at the same time.

#### Screenshot of http demo

The URL for the demo is http://127.0.0.1:5001. On the screensht there are two videos streamed by the server.

![alt text](doc/http-demo-1.png)


### Implementation

It was possible ans easier to use POSIX ucontext (with makecontext(), setcontext(), getcontext() and swapcontext()) but I choosed to use setjmp() / longjmp() C functions. The hard part is how to get a different stack for each fiber. There are several way to do this but I used sigaltstack() which allows to set an alternate stack for a signal handler. 

### Other fibers / coroutines implementations

There are many libraries implementing fibers and coroutines.

The C++ boost libraries have both and the implementation is fast and easy to use. I definitely recommend this.

Interstingly, in the http example, there is a base64 decoder with a coroutine like implementation. The trick used there is initially due to Simon Tatham.

### API documentation

There are two kinds of objects in the library schedulers and fibers. A fiber can't run by itself and must be started in a scheduler.

