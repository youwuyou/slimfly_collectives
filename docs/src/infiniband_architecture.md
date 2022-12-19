# InfiniBand Architecture

- based on Virtual Cut Through (VCT)


> RDMA over InfiniBand: Package from one server at user application layer does not need to be copied into the kernel layer and the hardware layer but can be directed transported from the buffer of server 1 to HCAs and then to the buffer of server 2



## Routing on Slim Fly at CSCS

**Matter of interest:** Intra-subnet routing


**Hardware components:**

>Switch: A device that moves packets from one link to another of the same IB subnet (layer2)

>Host Channel Adapter (HCA): device that terminates an IB link and executes transport-level functions and support the verbs interface

>IB Router: A device that transports packets between different IBA subnets. (inter-subnet routing)


>Bridge/Gateway: IB to Ethernet

---

**Other Concepts**

>Virtual Lanes (VLs): Each virtual lane uses different buffers to send its packet towards the other side. VL-15 moves traffic of subnet manager specifically; VL-0-7 to move traffic, by default all data goes through VL-0. In order to provide different lane of service based on different usage, we need to map applications to specific virtual lanes.

- Linear Forwarding Table (LFT): is indexed using the destination LID (DLID) of the packet. Helps to determine the output port of a packet at a switch.

- Service Level (SL): 4-bit meta-data contained in each packet's header, used to determine the outgoing VL by a *SL-to-VL table* => thus packets can change virtual lanes at each hop, hardware with different number of virtual lanes can be used together.


>Subnet Manager: most important entity in the network, does all configurations using parameters. Every subnet must have at least one.


```bash
[dphpc@slimflysmw benchmarks]$ sminfo
ibwarn: [55188] mad_rpc_open_port: can't open UMAD port ((null):0)
sminfo: iberror: failed: Failed to open '(null)' port '0'
[dphpc@slimflysmw benchmarks]$ saquery -s
ibwarn: [55250] sa_get_handle: umad_open_port on port (null):0 failed
ibpanic: [55250] main: Failed to bind to the SA: Permission denied
```


"within a subnet the switches are of layer-2-switching"?


**Addressing:**

i). Physical Adress: Global Unique Identifier (GUID)

- 64 bits adress

- persistent through reboots => no changes when rebooting

- a switch has only one GUID (Port: identify by GUID of the node and the port number)


**ii). L2 Switching Addressing Local Identifier (LID)**

- 16 bits adress

- used when moving traffic from one node to another node within a subnet

- assigned by the *Subnet Manager* when port becomes active

- Not persistent through reboots

- Use the LID Mask Control (LMC) value

$LMC=x \Rightarrow \text{No.}LIDs = 2^x$



iii). Global Identifier (GID)

- layer 3 address

- 128 bits, a combination of the port GUID and the subnet prefix



**Implementation:** 

>All comm unication up to and include the transport layer is
>specified and implemented within the *switches* and *Host >Channel Adapters (HCAs)*




# Ethernet

Moving data between the InfiniBand and the Ethernet environment needs the hardware component "Gateway"

