# libfiber

userland cooperative threads aka fibers aka green threads aka coroutines.

## Introduction

This small librairy implements user land cooperative threads, they are called fibers.

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
