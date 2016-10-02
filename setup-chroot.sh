#!/bin/bash

mkdir -p chroot/tmp
mkdir -p chroot/bin
mkdir -p chroot/var/tmp/chaosd

cp chaos/chaosd chroot/bin
cp chaos/FILE chroot/bin
cp chaos/time chroot/bin
cp chaos/server chroot/bin

for f in `find chroot -type f -print | xargs otool -L | grep -v chroot | grep compatibility | sed s/\(.\*// | sort | uniq`; do echo $f; mkdir -p chroot`dirname $f`; cp -v $f chroot$f; done
for f in `find chroot -type f -print | xargs otool -L | grep -v chroot | grep compatibility | sed s/\(.\*// | sort | uniq`; do echo $f; mkdir -p chroot`dirname $f`; cp -v $f chroot$f; done
for f in `find chroot -type f -print | xargs otool -L | grep -v chroot | grep compatibility | sed s/\(.\*// | sort | uniq`; do echo $f; mkdir -p chroot`dirname $f`; cp -v $f chroot$f; done
for f in `find chroot -type f -print | xargs otool -L | grep -v chroot | grep compatibility | sed s/\(.\*// | sort | uniq`; do echo $f; mkdir -p chroot`dirname $f`; cp -v $f chroot$f; done
cp /usr/lib/dyld chroot/usr/lib

ln -s `pwd`/chroot/var/tmp/chaosd /var/tmp

echo "Run: sudo chroot -u JoshuaEckroth chroot bin/chaosd -n -D3"
echo "And: sudo chroot -u JoshuaEckroth chroot bin/server -D3"

