# MultiROM
MultiROM is a one-of-a-kind multi-boot solution. It can boot android ROM while
keeping the one in internal memory intact or boot Ubuntu without formating
the whole device. MultiROM can boot either from internal memory of the device
or from USB flash drive.

XDA threads:
* grouper: http://forum.xda-developers.com/showthread.php?t=2011403
* flo: http://forum.xda-developers.com/showthread.php?t=2457063
* mako: http://forum.xda-developers.com/showthread.php?t=2472295

###Build
Clone repo to folder `system/extras/multirom` inside Android 4.x source tree.
MultiROM also needs libbootimg (https://github.com/Tasssadar/libbootimg)
in folder `system/extras/libbootimg`. Use something like this to build:

    . build/envsetup.h
    lunch full_grouper-userdebug
    make -j4 multirom trampoline

To build installation ZIP file, use `multirom_zip` target:

    make -j4 multirom_zip
