#!/bin/sh

OS_NAME="`uname`"
echo  $OS_NAME


rm -fr build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release .. 
make

if [ $OS_NAME = "Darwin" ];then
  # Mac OS X 操作系统
  echo "mac os, and will install"
  sudo make install
  sudo update_dyld_shared_cache
else
  # GNU/Linux操作系统
  echo "linux, and will install"
  sudo make install
  sudo ldconfig
fi
