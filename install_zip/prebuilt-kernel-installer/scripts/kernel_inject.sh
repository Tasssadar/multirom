#!/sbin/sh
BOOT_DEV="$(cat /tmp/bootdev)"

if [ ! -e "$BOOT_DEV" ]; then
    echo "BOOT_DEV \"$BOOT_DEV\" does not exist!"
    return 1
fi

chmod 755 /tmp/kernel_inject
/tmp/kernel_inject --inject="$BOOT_DEV" --kernel="/tmp/kernel"
return $?
