#!/sbin/sh
BOOT_DEV="$(cat /tmp/bootdev)"

if [ ! -e "$BOOT_DEV" ]; then
    echo "BOOT_DEV \"$BOOT_DEV\" does not exist!"
    return 1
fi

chmod 755 /tmp/multirom/trampoline
chmod 755 /tmp/multirom/busybox
chmod 755 /tmp/multirom/lz4
/tmp/multirom/trampoline --inject="$BOOT_DEV" --mrom_dir="/tmp/multirom" -f
return $?
