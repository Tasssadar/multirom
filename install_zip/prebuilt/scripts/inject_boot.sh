#!/sbin/sh
BUSYBOX="/tmp/multirom/busybox"
LZ4="/tmp/lz4"
BOOT_DEV="$(cat /tmp/bootdev)"

CMPR_GZIP=0
CMPR_LZ4=1

if [ ! -e "$BOOT_DEV" ]; then
    echo "BOOT_DEV \"$BOOT_DEV\" does not exist!"
    return 1
fi

dd if=$BOOT_DEV of=/tmp/boot.img
/tmp/unpackbootimg -i /tmp/boot.img -o /tmp/
if [ ! -f /tmp/boot.img-zImage ] ; then
    echo "Failed to extract boot.img"
    return 1
fi

rm -r /tmp/boot
mkdir /tmp/boot

cd /tmp/boot
rd_cmpr=-1
magic=$($BUSYBOX hexdump -n 4 -v -e '/1 "%02X"' "../boot.img-ramdisk.gz")
case "$magic" in
    1F8B*)           # GZIP
        $BUSYBOX gzip -d -c "../boot.img-ramdisk.gz" | $BUSYBOX cpio -i
        rd_cmpr=CMPR_GZIP;
        ;;
    02214C18)        # LZ4
        $LZ4 -d "../boot.img-ramdisk.gz" stdout | $BUSYBOX cpio -i
        rd_cmpr=CMPR_LZ4;
        ;;
    *)
        echo "invalid ramdisk magic $magic"
        ;;
esac

if [ rd_cmpr == -1 ] || [ ! -f /tmp/boot/init ] ; then
    echo "Failed to extract ramdisk!"
    return 1
fi

# copy trampoline
if [ ! -e /tmp/boot/main_init ] ; then 
    mv /tmp/boot/init /tmp/boot/main_init
fi
cp /tmp/multirom/trampoline /tmp/boot/init
chmod 750 /tmp/boot/init

# crete ueventd and watchdogd symlink
if [ -L /tmp/boot/sbin/ueventd ] ; then
    ln -sf ../main_init /tmp/boot/sbin/ueventd
fi
if [ -L /tmp/boot/sbin/watchdogd ] ; then
    ln -sf ../main_init /tmp/boot/sbin/watchdogd
fi

# pack the image again
cd /tmp/boot

case $rd_cmpr in
    CMPR_GZIP)
        find . | $BUSYBOX cpio -o -H newc | $BUSYBOX gzip > "../boot.img-ramdisk.gz"
        ;;
    CMPR_LZ4)
        find . | $BUSYBOX cpio -o -H newc | $LZ4 stdin "../boot.img-ramdisk.gz"
        ;;
esac

cd /tmp
/tmp/mkbootimg --kernel boot.img-zImage --ramdisk boot.img-ramdisk.gz --cmdline "$(cat boot.img-cmdline)" --base $(cat boot.img-base) --output /tmp/newboot.img

if [ ! -e "/tmp/newboot.img" ] ; then
    echo "Failed to inject boot.img!"
    return 1
fi

echo "Writing new boot.img..."
dd bs=4096 if=/tmp/newboot.img of=$BOOT_DEV
return $?
