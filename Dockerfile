FROM ubuntu:xenial

ENV LANG C
ENV DEBIAN_FRONTEND noninteractive
ENV DEBCONF_NONINTERACTIVE_SEEN true

RUN mkdir -p /opt/shellinabox
WORKDIR /opt/shellinabox
COPY . .

RUN apt-get update && \
    apt-get install -y git libssl-dev libpam0g-dev zlib1g-dev dh-autoreconf && \
    apt-get clean

RUN autoreconf -i && \
    ./configure --prefix=/usr && \
    make && \
    make install && \
    make clean

RUN mkdir -p ssl && chown -R nobody:nogroup ssl

EXPOSE 4200
ENTRYPOINT shellinaboxd --cert=ssl --verbose
