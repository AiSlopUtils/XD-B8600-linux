FROM ubuntu:22.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libmpc3 \
        libmpfr6 \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
