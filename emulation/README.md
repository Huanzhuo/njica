# Emulation of Distributed MEICA

## Getting Started

Please run follow steps to setup the emulator and  run a simple store and forward example.

Assume the source directory of `in-network_bss` project is `~/in-network_bss`.

1. Create the testbed VM using Vagrant on your host OS.

```bash
cd ~/in-network_bss || exit
vagrant up testbed
```

Then run `vagrant ssh testbed` to login into the VM.

Following steps should be run **inside the VM**.

2. Run test to make sure the `ComNetsEmu` is installed correctly.

```bash
cd ~/comnetsemu
sudo make test
```

Only run following steps when all tests passed without any errors.
Otherwise, please create issues on [Github](https://github.com/stevelorenz/comnetsemu/issues) or send Emails to Zuo Xiang.

3. Build the Docker image for in-network_bss

```bash
cd /vagrant
./build.sh
```

After this step, you should see the image with name `in-network_bss` when running `docker image ls`.

You should change your current path to `/vagrant/emulation` (inside the VM, of course) for following steps.

4. Build executables for VNFs.

```bash
sudo ./build_executable.py
```

After this step, you should find `meica_vnf` ELF file in `./build/` directory.

5. Run the multi-hop network emulation script with store and forward mode.

```bash
sudo ./topology.py
```

Now you should see the pop-up window for logs of the Ryu SDN controller running the application `./multi_hop_controller.py`.
And you should also see the prompt `mininet>` when the network configuration is finished.
If you check the CPU usage inside the VM using `htop`, three VNF processes are heavily using the second CPU core (Because they are based on DPDK with polling mode).

6. Run server and client programs inside the corresponded container.

```bash
mininet> xterm client server
```

Then two windows are popped up, you can identify the client and server by looking at the host name (e.g. `@client`) in the shell.

Then please firstly run `server.py` inside the server's shell and then `client.py` in the clients shell (use `-h` to check the CLI options). (Currently, this order must
be kept manually.)

Run server and client with the default options:

```python
root@server# python ./server.py

root@client# python ./client.py
```

Example (could be outdated...) of the output of the client and server:

1. Server:

```bash
* Server runs.
* Start receiving chunks.
* Received the first chunk, total chunk number: 1098
* Received all chunks.
Elapsed time of func: recv_chunks: 21.1983 seconds
* Start running centralized MEICA.
```

2. Client:

```bash
* Client runs.
* Message size: 1536000. Start sending 1098 chunks to the server...
* 1098 chunks are sent.
* Wait for the ACK from the server...
ACK: OK
* Total service latency: 16.484894037246704 seconds.
```
