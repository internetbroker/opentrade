#!/usr/bin/env bash

yum install -y epel-release
yum update -y
yum install -y libpq-devel sqlite-devel git python3-devel make gcc cmake clang vim ctags tbb-devel wget dos2unix libtool apr-devel apr-util-devel valgrind gdb openssl-devel python2
pip3 install numpy yapf
wget https://github.com/quickfix/quickfix/archive/v1.15.1.tar.gz
tar xzf v1.15.1.tar.gz
cd quickfix-1.15.1/
./bootstrap 
./configure
make install
cd -
wget https://www-eu.apache.org/dist/logging/log4cxx/0.10.0/apache-log4cxx-0.10.0.tar.gz
tar xzf apache-log4cxx-0.10.0.tar.gz 
cd ~/apache-log4cxx-0.10.0
./configure
find . -name inputstreamreader.cpp | xargs sed -i 's/using namespace log4cxx;/using namespace log4cxx;\n#include <cstring>/g'
find . -name socketoutputstream.cpp | xargs sed -i 's/using namespace log4cxx;/using namespace log4cxx;\n#include <cstring>/g'
find . -name loggingevent.cpp | xargs sed -i 's/0x/(char)0x/g' 
find . -name locationinfo.cpp | xargs sed -i 's/0x/(char)0x/g' 
find . -name objectoutputstream.cpp | xargs sed -i 's/0x/(char)0x/g' 
find . -name console.cpp | xargs sed -i 's/using namespace log4cxx;/using namespace log4cxx;\n#include <cstring>/g'
find . -name domtestcase.cpp | xargs sed -i 's/0x/(char)0x/g' 
make install
cd -
wget https://github.com/SOCI/soci/archive/3.2.3.tar.gz
tar xzf 3.2.3.tar.gz 
cd soci-3.2.3/
cmake src
make install
cd -
wget https://dl.bintray.com/boostorg/release/1.66.0/source/boost_1_66_0.tar.gz
tar xzf boost_1_66_0.tar.gz
cd boost_1_66_0
cd /usr/include; ln -s python3.6m python3.6; cd -
./bootstrap.sh --with-python-root=/usr/bin/python3
ln -s /usr/bin/python3 python; PATH=$PATH:. ./b2 --debug-configuration install
cd -
yum install -y postgresql-server & /usr/bin/postgresql-setup initdb & service postgresql start
