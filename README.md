# Virtual Networking Quick Start

## List hosts

Each `Node` in the network requires a Virtual Network Interface Card (VNIC)
to be able to pass messages from one node to another.

### Step 1 - Compilation
The code can be compiled with the `make` command. Ensure that it is compiled
on the same system it will be executed on, or at the very least, an identical
system 
(There are no guarantees the code will run when compiled on a 
different system). It is important that the kernel version 
of the compiling machine
exactly matches the kernel version of the target machine.

Kernel version can be queried using
```
uname -r
```

The script 
```
./compile
```
can be run to perform compilation instead of `make`. This syncs the code
to a remote (or virtual) machine which should be named `kdev` in the
`/etc/hosts` file.
It then uses `ssh` to call `make` on the remote machine.

When using the `./compile` script, the code will be synced to the same
folder on the remote machine as the folder it was called from on the host
machine. If this folder doesn't exist, it is automatically created.

Any arguments passed to `./compile` will subsequently be passed to `make`
once the code has been synced to `kdev`.

### Step 2
In the file `ip_mappings.txt`, create a list of newline separated IP
addresses. This file can contain domain names which will be automatically
resolved when the program runs.  
Accepted examples include:
* 192.168.0.1
* 10.11.12.13
* localhost
* www.google.co.uk

Any IP address any domain name which can be resolved by your own system through
the `/etc/hosts` or any domain name which can be resolved by a connected
DNS server is valid.

### Step 3
Run the bash script
```
./load_vnics
```
This will read each line from the
`ip_mappings.txt` file, and issue the command
```
insmod vnic.ko
```
with the resolved host names from `ip_mappings.txt`. One VNIC will be created
for each IP address within `ip_mappings.txt`. The first IP address will belong
to `VNIC0`, which will be connected to the network simulator. All subsequent
IP addresses will map to the nodes within the network.

It is therefore **Important** that the first IP address is one that will
not be needed by the applications being run over the network simulator.