#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

DOCKER_IMAGE=${DOCKER_IMAGE:-soompay/soomd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/soomd docker/bin/
cp $BUILD_DIR/src/soom-cli docker/bin/
cp $BUILD_DIR/src/soom-tx docker/bin/
strip docker/bin/soomd
strip docker/bin/soom-cli
strip docker/bin/soom-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
