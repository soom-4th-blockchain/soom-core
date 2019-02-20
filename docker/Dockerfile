FROM debian:stretch
LABEL maintainer="Soom Developers <dev@soompay.net>"
LABEL description="Dockerised SoomCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
