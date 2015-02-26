#!/sbin/sh
BOOT_DEV="$(cat /tmp/bootdev)"

if [ ! -e "$BOOT_DEV" ]; then
    echo "BOOT_DEV \"$BOOT_DEV\" does not exist!"
    return 1
fi

/tmp/multirom/trampoline --inject="$BOOT_DEV" --mrom_dir="/tmp_multirom" -f
return $?
