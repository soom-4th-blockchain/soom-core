# Seeds

Utility to generate the seeds.txt list that is compiled into the client
(see [src/chainparamsseeds.h](/src/chainparamsseeds.h) and other utilities in [contrib/seeds](/contrib/seeds)).

Be sure to update `MIN_PROTOCOL_VERSION` in `makeseeds.py` to include the current version.

The seeds compiled into the release are created from the current gateway list, like this:

    soom-cli gatewaylist full > gwlist.json
    python3 makeseeds.py < gwlist.json > nodes_main.txt
    python3 generate-seeds.py . > ../../src/chainparamsseeds.h

## Dependencies

Ubuntu:

    sudo apt-get install python3-dnspython
