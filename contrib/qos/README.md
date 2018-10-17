### QoS (Quality of service) ###

This is a Linux bash script tht will set up tc to limit the outgoing bandwidth for connections to the Soom network. It limits outbound TCP traffic with a source or destination port of 16099, but not if the destination IP is within a LAN.

This means one can have an always-on soomd instance running, and another local soomd/soom-qt instance which connects to this node and receives blocks from it.
