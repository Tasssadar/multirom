#!/sbin/sh
BOOT_DEV="$(cat /tmp/bootdev)"
LOKI="/tmp/loki_tool"

# Cleanup
rm -rf /tmp/boot /tmp/boot.img /tmp/bootimg.cfg /tmp/zImage /tmp/initrd.img /tmp/second.img /tmp/dtb.img

dd if=$BOOT_DEV of=/tmp/boot.img
/tmp/loki_tool unlok /tmp/boot.img /tmp/boot.lok
rm /tmp/boot.img
mv /tmp/boot.lok /tmp/boot.img
rm -rf /tmp/boot.lok

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

if [ rd_cmpr == -1 ] || [ ! -f /tmp/boot/init ] ; then
    echo "Failed to extract ramdisk!"
    return 1
fi

