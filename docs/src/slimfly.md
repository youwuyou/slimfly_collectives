# Low-diameter Topology

- Example: Slim Fly, Dragonfly, Xpander, HyperX ...


## Slim Fly

- low-diameter topology with diameter 2

- utilizes high-radix routers

- lowest network diameter among the low-diameter topologies (Besta et al. 2020)

---
*Optimization problem:*

- objective: maximize the number of endpoints for a given router radix

- constraints: fixed network diameter and router radix
---

### Advantages

Using cost-effective fiber cables

- significantly lower construction and operating costs

- lower latency


### Problems

>Reference: Multipath Routing for
>Low-Diameter Network Topologies
>on InfiniBand Architecture (Nils Blach)

- low-diameter topologies utilize high-radix switches to achieve lower latency and higher bandwidth

**Problem 1:** Congestion-prone traffic

- Reason: There is almost always only a single shortest path available between endpoint paris. Thus oversubscribing shortest paths due to inadequate routing (traditional routing schemes)

- Approach: diversity of shortest paths needed

=> exploit both minimal and almost-minimal paths

=> combines layered routing and InfiniBand's LID masking



# FatPaths

Reference: [FatPaths: Routing in Supercomputers and Data Centers when Shortest Paths Fall Short](arXiv:1906.10885)

- the first routing architecture targeted the issue regarding the lack of the path diversity for low-diameter topologies deployed on Ethernet

