#!/bin/bash
d=./archive/`date "+%Y%m%d-%H%M%S"`
mkdir -p $d
mv ./store $d
mv ./fixlog $d
mv ./fixstore $d
mv ./logs $d
rootd=`pwd`
cd $d/store
mkdir store && ls pnl* | xargs -I file sh -c "tail -n 30000 file | awk 'NR % 4 == 0' > store/file" && mv store $rootd
cd -
tar czf $d.tar.gz $d && /bin/rm -rf $d
