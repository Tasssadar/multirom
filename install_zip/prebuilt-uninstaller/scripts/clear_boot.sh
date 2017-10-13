#!/sbin/sh
BUSYBOX="/tmp/busybox"
LZ4="/tmp/lz4"
BOOT_DEV="$(cat /tmp/bootdev)"
RD_ADDR="$(cat /tmp/rd_addr)"

CMPR_GZIP=0
CMPR_LZ4=1

if [ ! -e "$BOOT_DEV" ]; then
    echo "BOOT_DEV \"$BOOT_DEV\" does not exist!"
    return 1
fi

dd if=$BOOT_DEV of=/tmp/boot.img
/tmp/bbootimg -x /tmp/boot.img /tmp/bootimg.cfg /tmp/zImage /tmp/initrd.img /tmp/second.img /tmp/dtb.img
if [ ! -f /tmp/zImage ] ; then
    echo "Failed to extract boot.img"
    return 1
fi

rm -r /tmp/boot
mkdir /tmp/boot

cd /tmp/boot
rd_cmpr=-1
magic=$($BUSYBOX hexdump -n 4 -v -e '/1 "%02X"' "../initrd.img")
case "$magic" in
    1F8B*)           # GZIP
        $BUSYBOX gzip -d -c "../initrd.img" | $BUSYBOX cpio -i
        rd_cmpr=CMPR_GZIP;
        ;;
    02214C18)        # LZ4
        $LZ4 -d "../initrd.img" stdout | $BUSYBOX cpio -i
        rd_cmpr=CMPR_LZ4;
        ;;
    *)
        echo "invalid ramdisk magic $magic"
        ;;
esac

if [ rd_cmpr == -1 ] || [ ! -e /tmp/boot/init -a ! -L /tmp/boot/init ] ; then
    echo "Failed to extract ramdisk!"
    return 1
fi

# restore init
if [ -e /tmp/boot/main_init ] ; then
    if [ -e /tmp/boot/init.real ] ; then
        rm /tmp/boot/init.real
        mv /tmp/boot/main_init /tmp/boot/init.real
    else
        rm /tmp/boot/init
        mv /tmp/boot/main_init /tmp/boot/init
    fi
fi

chmod 750 /tmp/boot/init

# restore ueventd and watchdogd symlink
if [ -L /tmp/boot/sbin/ueventd ] ; then
    ln -sf ../init /tmp/boot/sbin/ueventd
fi
if [ -L /tmp/boot/sbin/watchdogd ] ; then
    ln -sf ../init /tmp/boot/sbin/watchdogd
fi

if [ -e /tmp/boot/mrom.fstab ] ; then
    rm /tmp/boot/mrom.fstab
fi

# Remove encryption files
rm -rf /tmp/boot/mrom_enc

# pack the image again
cd /tmp/boot

case $rd_cmpr in
    CMPR_GZIP)
        find . | $BUSYBOX cpio -o -H newc | $BUSYBOX gzip > "../initrd.img"
        ;;
    CMPR_LZ4)
        find . | $BUSYBOX cpio -o -H newc | $LZ4 stdin "../initrd.img"
        ;;
esac

echo "bootsize = 0x0" >> /tmp/bootimg.cfg
if [ -n "$RD_ADDR" ]; then
    echo "Using ramdisk addr $RD_ADDR"
    echo "ramdiskaddr = $RD_ADDR" >> /tmp/bootimg.cfg
fi

cd /tmp

dtb_cmd=""
if [ -f "dtb.img" ]; then
    dtb_cmd="-d dtb.img"
fi

/tmp/bbootimg --create newboot.img -f bootimg.cfg -k zImage -r initrd.img $dtb_cmd

if [ ! -e "/tmp/newboot.img" ] ; then
    echo "Failed to inject boot.img!"
    return 1
fi

echo "Writing new boot.img..."
dd bs=4096 if=/tmp/newboot.img of=$BOOT_DEV
return $?
