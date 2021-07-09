#!/bin/bash

docker build -t in-network_bss -f ./Dockerfile .
docker image prune --force
