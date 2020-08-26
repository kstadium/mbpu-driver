#! /bin/bash
sudo ~/.mbpu/bin/mcap -x 0x1818 -p ~/.mbpu/bin/MDL_TOP_tandem2.bit

sudo insmod ~/.mbpu/mdlx.ko poll_mode=1
sleep 3
sudo chmod a+rw /dev/mdlx*
