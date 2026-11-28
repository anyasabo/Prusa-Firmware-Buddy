# LWIp's MDNS responder app

This is the MDNS responder app from LwIP. For some reason, our LwIP copy is
missing the apps directory (likely back then someone has imported it without
it). We don't want to do a full reimport and upgrade (there has been some
changes to the source codes there). For that reason, the MDNS responder is
imported separately.

It's almost vanilla taken from 347054b329d141b858cc7180bf2aa23c672dc6d7, with
the three later upstream mDNS fixes up to master
a33328d0ee94ad4d4656dcf80cc60b83cba941e2 (ab2f69c7, 3a3182b0, 07de8c51), and
minor adjustments:

* Formatting (automatic commit hook)
* Adjustment of headers (because it's not sitting in the apps directory)
