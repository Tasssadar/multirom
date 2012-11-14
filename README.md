#MultiROM for Nexus 7

// TODO: write info about multirom

###Build
Clone repo to some folder inside Android 4.1.x source tree, I use `/system/extras/multirom`. Then just

    . build/envsetup.h
    lunch full_grouper-userdebug
    make multirom trampoline -j4
