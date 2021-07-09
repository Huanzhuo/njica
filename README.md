[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
Network Joint Independent Componet Analysis (NJICA)

## Table of Contents
- [Table of Contents](#table-of-contents)
- [Description](#description)
- [Requirements](#requirements)
- [Getting Started](#getting-started)
- [Run NJICA in the Emulator](#run-njica-in-the-emulator)
- [Citation](#citation)
- [About Us](#about-us)

## Description

This application emulates the Network Joint Independent Componet Analysis (NJICA) in **[comnetsemu](https://git.comnets.net/public-repo/comnetsemu)**.
The implementation of Newton's Iteration is based on FastICA in **[scikit-learn](https://scikit-learn.org/stable/)**. 

## Requirements

Please install `vagrant` and `Virtualbox` on the host OS to build the testbed VM.

## Getting Started

Please run the following steps to set up the emulator.

Assume the source directory of `njica` project is `~/njica`.

1. Create the testbed VM using Vagrant on your host OS.

    ```bash
    cd ~/njica || exit
    vagrant up testbed
    ```

    Then run `vagrant ssh testbed` to login into the VM. Following steps should be run **inside the VM**.

<!-- 2. Install `docker-ce` and add docker into user group
    ```bash
    sudo apt-get update
    sudo apt-get install  apt-transport-https  ca-certificates curl  software-properties-common
    curl -fsSL  https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add
    sudo add-apt-repository "deb [arch=amd64]  https://download.docker.com/linux/ubuntu bionic stable" 
    sudo apt-get update
    sudo apt-get install docker-ce

    sudo groupadd docker
    sudo gpasswd -a vagrant docker
    newgrp docker

    cd /home/vagrant/comnetsemu/test_containers || exit
    sudo bash ./build.sh
    ``` -->

2. Upgrade ComNetsEmu Python module and all dependencies automatically inside VM
    ```bash
    cd ~/comnetsemu/util
    bash ./install.sh -u
    ```

3. Run test to make sure the `ComNetsEmu` is installed correctly (optional).
    ```bash
    cd ~/comnetsemu
    sudo make test
    ```
    Only run following steps when all tests passed without any errors. Otherwise, please create issues on [Github](https://github.com/stevelorenz/comnetsemu/issues) from Zuo Xiang.

## Run NJICA in the Emulator
1. Build the Docker image for in-network_bss

    ```bash
    cd /vagrant
    ./build.sh
    ```

    After this step, you should see the image with name `in-network_bss` when running `docker image ls`.

    You should change your current path to `/vagrant/emulation` (inside the VM, of course) for following steps.

2. Build executables for VNFs.

    ```bash
    cd /vagrant/emulation
    sudo ./build_executable.py
    ```

    After this step, you should find `meica_vnf` ELF file in `./build/` directory.

3. Run the multi-hop network emulation script with store and forward mode.

    ```bash
    sudo ./topology.py
    ```

    Now you should see the pop-up window for logs of the Ryu SDN controller running the application `./multi_hop_controller.py`.
    And you should also see the prompt `mininet>` when the network configuration is finished.
    If you check the CPU usage inside the VM using `htop`, three VNF processes are heavily using the second CPU core (Because they are based on DPDK with polling mode).

4. Run server and client programs inside the corresponded container.

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

## Citation

## About Us

We are researchers at the Deutsche Telekom Chair of Communication Networks (ComNets) at TU Dresden, Germany. Please feel free to contact us with any questions you may have.

* **Huanzhuo Wu** - huanzhuo.wu@tu-dresden.de or wuhuanzhuo@gmail.com
* **Zuo Xiang** - zuo.xiang@tu-dresden.de 
* **Yunbin Shen** - yunbin.shen@mailbox.tu-dresden.de or shenyunbin@outlook.com

