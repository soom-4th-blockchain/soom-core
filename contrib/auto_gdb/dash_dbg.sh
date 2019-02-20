#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.soomcore/soomd.pid file instead
soom_pid=$(<~/.soomcore/testnet3/soomd.pid)
sudo gdb -batch -ex "source debug.gdb" soomd ${soom_pid}
