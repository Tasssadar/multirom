#!/bin/sh
FSTAB="$1"
DEST_FILE="$2"

if [ $# != "2" ]; then
    echo "Usage: $0 [path to fstab] [path to dest dir]"
    exit 1
fi

itr=-1
for tok in $(grep -v "^ *#.*$" "$FSTAB"); do
    case "$itr" in
        "-1") # mount point
            if [ "$tok" = "/boot" ]; then
                itr="0"
            fi
            ;;
        "0") # filesystem
            itr=$(($itr+1))
            ;;
        "1") # device
            if [ "${tok##/dev*}" ]; then
                echo "Unsupported fstab format!"
                exit 1
            fi

            echo "$tok" > "$DEST_FILE"
            echo "Used boot device: $tok"
            exit 0
            ;;
    esac
done

echo "Could not find /boot mountpoint in fstab!"
exit 1
