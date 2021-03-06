FROM ubuntu:20.04

# Install dependencies for DPDK and XDP
# - Clang LLVM are required to build XDP programs.
RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install -y wget build-essential pkg-config python3 meson ninja-build \
    libnuma-dev libpcap-dev \
    libelf-dev clang llvm gcc-multilib linux-tools-common

WORKDIR /opt/

# Build xdp-tools from source (no deb package available)
# xdp-tools has libbpf included which is required to build AF_XDP PMD driver of DPDK.
ENV XDP_TOOLS_VER="0.0.3"
ENV XDP_TOOLS_DIR="/opt/xdp-tools"
RUN mkdir -p ${XDP_TOOLS_DIR} && \
    wget https://github.com/xdp-project/xdp-tools/releases/download/v${XDP_TOOLS_VER}/xdp-tools-${XDP_TOOLS_VER}.tar.gz && \
    tar -zxvf xdp-tools-${XDP_TOOLS_VER}.tar.gz -C ./xdp-tools --strip-components 1 && \
    cd ./xdp-tools && ./configure && make && make install && cd ./lib/libbpf/src && make install
# Let the build system and linker to find the libbpf.so
ENV PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:/usr/lib64/pkgconfig
ENV LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/usr/lib64

# Install DPDK packages.
ENV DPDK_VER="19.11"
ENV RTE_SDK="/opt/dpdk"
ENV RTE_TARGET="x86_64-native-linuxapp-gcc"
RUN mkdir -p ${RTE_SDK} && \
    wget http://fast.dpdk.org/rel/dpdk-${DPDK_VER}.tar.xz && \
    tar -xJf dpdk-${DPDK_VER}.tar.xz -C ./dpdk --strip-components 1 && \
    debian_frontend="noninteractive" apt-get install -y \
    dpdk dpdk-dev libdpdk-dev

# Remove unused files.
RUN rm -rf /opt/xdp-tools-${XDP_TOOLS_VER}.tar.gz /opt/dpdk-${DPDK_VER}.tar.xz

# Install FFPP dependencies and dev tools.
RUN apt-get update && \
    debian_frontend="noninteractive" apt-get install -y \
    git libczmq-dev libjansson-dev golang \
    libcmocka-dev libcpufreq-dev gcovr libmsgsl-dev python3-pybind11 \
    python3-pip python3-zmq python3-dev python3-numpy \
    bash-completion cppcheck clang-tidy net-tools iproute2 iputils-ping tcpdump \
    libtins-dev
RUN pip3 install -q docker cffi

# Build FFPP library
ENV FFPP_PATH /ffpp
RUN mkdir -p ${FFPP_PATH}
WORKDIR /tmp
RUN git clone https://github.com/stevelorenz/build-vnf.git && \
    cd ./build-vnf && \
    # Use a specific commit to update FFPP library without re-setup everything.
    git reset --hard 8e7a80f880e83011aa84a699dfd9894b9df289bf && \
    cp -r ./ffpp/* /ffpp
WORKDIR ${FFPP_PATH}/user
RUN make release && make install
ENV PKG_CONFIG_PATH=${PKG_CONFIG_PATH}:/usr/local/lib/x86_64-linux-gnu/pkgconfig
ENV LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/usr/local/lib/x86_64-linux-gnu

# Install in-network_bss dependencies
RUN apt-get update && \
    DEBIAN_FRONTEND="noninteractive" apt-get install -y \
    python3-dev python3-numpy python3-scipy \
    libsndfile1-dev ffmpeg net-tools telnet procps python3-ipdb \
    libboost-dev libboost-program-options-dev libasio-dev
RUN pip3 install -q docker cffi progressbar2 museval scapy librosa

RUN mkdir -p /in-network_bss
COPY . /in-network_bss
WORKDIR /in-network_bss/emulation
RUN make

# APT cleanup.
RUN rm -rf /var/lib/apt/lists /var/cache/apt/archives

CMD ["bash"]
