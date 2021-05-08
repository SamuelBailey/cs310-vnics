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
The configuration is specified by `vnic_config.json`. Copy this file from the
`example_config/` folder into the root directory of this repository, and alter any values
contained within to change the configuration of the VNICs.

Any IP address any domain name which can be resolved by your own system through
the `/etc/hosts` or any domain name which can be resolved by a connected
DNS server is valid.

### Step 3
Run the bash script
```
./load_vnics
```
This will read each entry from the
`vnic_config.json` file, and setup the VNICs. The first two VNICs in the
configuration are designed to be used by the simulator.
It is important that the `id` is incremented between each VNIC.
