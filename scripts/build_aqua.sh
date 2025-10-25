#!/bin/sh
set -e

. ./config.sh

pushd $AQUA

cd gv
echo | bob clean # XXX
export CC AR
bob build

popd
