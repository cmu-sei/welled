FROM ubuntu:16.04

COPY . /app
WORKDIR /app/src

RUN apt-get update                                                                  && \
    apt-get install -y libcurl4-openssl-dev                                         && \
    apt-get install -y libnl-3-dev libnl-genl-3-dev libnl-route-3-dev pkg-config    && \
    apt-get install -y libpng-dev libglib2.0-dev

RUN make x86_64-Linux
WORKDIR /app/dist/x86_64-Linux