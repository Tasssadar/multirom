# MultiROM
MultiROM is a one-of-a-kind multi-boot solution. It can boot android ROM while
keeping the one in internal memory intact or boot Ubuntu without formating
the whole device. MultiROM can boot either from internal memory of the device
or from USB flash drive.

XDA threads:
* grouper: http://forum.xda-developers.com/showthread.php?t=2011403
* flo: http://forum.xda-developers.com/showthread.php?t=2457063
* mako: http://forum.xda-developers.com/showthread.php?p=46223377
* hammerhead: http://forum.xda-developers.com/showthread.php?t=2571011

### Sources
MultiROM uses git submodules, so you need to clone them as well:

    git clone https://github.com/Tasssadar/multirom.git system/extras/multirom
    cd system/extras/multirom
    git submodule update --init

It also needs libbootimg:

    git clone https://github.com/Tasssadar/libbootimg.git system/extras/libbootimg

### Build
Clone repo to folder `system/extras/multirom` inside Android 4.x source tree.
You can find device folders on my github, I currently use OmniROM tree for
building (means branch android-4.4-mrom in device repos).
MultiROM also needs libbootimg (https://github.com/Tasssadar/libbootimg)
in folder `system/extras/libbootimg`. Use something like this to build:

    . build/envsetup.sh
    lunch full_grouper-userdebug
    make -j4 multirom trampoline

To build installation ZIP file, use `multirom_zip` target:

    make -j4 multirom_zip
