# Open MPI

## Basics

The Open MPI is comprised of three main functional areas:

**i). Modular Component Architecture (MCA)**
The center of the Open MPI design, acts as the backbone component architecture that provides management to all other layers. Provide services such as accepting run-time parameters from higher-level abstractions like `mpirun` and pass parameters through the component framework to each individual module.

**ii). Component Frameworks**
Back-end component framework used to manage modules of each main functional area of Open MPI. Each framework with different policy.

**iii). Modules**
Self-contained software components, each dedicated to a single job and is composable with other modules in run-time.


> Component programming provides library developer a fine-grained, run-time, user-controlled component selection mechanism. Convinient for our development.


### Setting MCA parameters

> Syntax:  `-mca <key> <value1, ..., valueN>`, where the `<key>` specifies the MCA module to receive the `<value>`. This is a shortcut to set environment variables of the form `OMPI_MCA_<key>=<value>` and it overrides the parameters set in `mca-params.conf` file.

> It is more advisable to pass `-mca` flag instead of `--mca`, even though the current openmpi version we are using recognizes it as a valid synonym. It is generally recommended to use the "-mca" option, rather than "--mca", when running "mpirun" with Open MPI. This is because the "-mca" option is more widely recognized and documented, and is the more commonly used syntax for specifying MCA parameters. Using "--mca" may cause confusion for users who are not familiar with this alternative syntax, or who are using a version of Open MPI that does not support the "--mca" option.

> The order in which the values are passed indicate the priority of them respectively.


## Components for MPI collective communication

### `basic`

> `basic`: the default component used when not using either shared memory or self, contains at least one set of implementations per collective operation.



### `sm`

> `sm`: collectives for use when Open MPI is running completely on a shared memory system


### `self`

> `self`: a special feature within MPI for use on the `MPI_COMM_SELF` communicator


### `tuned`

> `tuned` is an open MPI collective communications component


**Design:** tuned is designed with the following goals according to Fagg et al. from the university of Tennessee:

1. Multiple collective implementations

2. Multiple logical topologies

3. Wide range of fully tunable parameters

4. Effficient default decisions

5. Alternative user supplied compiled decision functions

6. User supplied selective decision parameter configuration files

7. Provide a means to dynamically create/alter decision functions at runtime


For the DPHPC project, the most important feature that we need is the 5th item, as we can make changes to the code and tune for better performance without fully replacing the current default rules.


**Performance:**


[(Source: Fagg et al)](https://link.springer.com/chapter/10.1007/978-0-387-69858-8_7)


---

# MPI Tutorial

> Page documented some most important concepts about MPI [(source)](https://mpitutorial.com/)

## Introduction and MPI installation

**Point-to-point communication:** communications involve one sender and receiver.


**Collective communication:** 

- eg. when a manger process needs to broadcast information to all of its worker processes.


## Blocking point-to-point communication

### Send and Receive

```C
MPI_Send(
    void* data,
    int count,
    MPI_Datatype datatype,
    int destination,
    int tag,
    MPI_Comm communicator)
```

```C
MPI_Recv(
    void* data,
    int count,
    MPI_Datatype datatype,
    int source,
    int tag,
    MPI_Comm communicator,
    MPI_Status* status)
```


> Process A decides a message needs to be sent to process B, process A packs up all of its necessary data into a buffer for process B. The buffer is called *envelopes*.

> The communication device (often a network) is responsible for routing the message to the proper location.

> Process B needs to acknowledge that it wants to receive A's data.

> Tagged messages to be received by `MPI_Recv()` have priority over the messages with other tags => will be buffered




## Groups and communicators




## Collective communication

We can classify the MPI collective operations using the following communication patterns:

**1. One-to-many/Many-to-one: single producer or consumer (eg. broadcast, reduce, Scatter(v), Gather(v))**

The data flow for this type of algorithm is unidirection. Virtual topologies can be used to determin the preceding and succeeding nodes in the algorithm.

- i). receive data from preceding nodes(s)

- ii). process data, if required

- iii). send data to succeeding node(s)



**2. Many-to-many: every participant is both producer and consumer (eg. barrier, alltoall, Allreduce, and Allgather(v))**



(source: [Pjesivac-Grbovic et al.](https://ieeexplore.ieee.org/document/1420226))
