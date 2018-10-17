Soom Core version 1.0.2.0
==========================

Release is now available from:

  <http://foutrhblockchain.org>

This is a new minor version release, bringing various bugfixes and other
improvements.

Please report bugs using the issue tracker at github:

  <https://github.com/soom-4th-blockchain/soom-core/issues>

Upgrading and downgrading
=========================

How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Soom-Qt (on Mac) or
soomd/soom-qt (on Linux).

Downgrade warning
-----------------

### Downgrade to 1.0.0.0

Downgrading to these versions does not require any additional actions, should be
fully compatible.

Notable changes
===============

Improve initial sync
--------------------

Some users had problems getting their nodes synced. The issue occured due to nodes trying to
get additional data from each available peer but not being able to process this data fast enough.
This was recognized as a stalled sync process and thus the process was reset. To address the issue
we limited sync process to 3 peers max now and the issue should no longer appear as long as there
are at least 4 connections.

