#!/sbin/sh
ROM_NAME="reference"

path=""
if [ -d "/data/media/multirom/roms" ] ; then
    path="/data/media/multirom/roms/$ROM_NAME"
elif [ -d "/data/media/0/multirom/roms" ] ; then
    path="/data/media/0/multirom/roms/$ROM_NAME"
else
    echo "Failed to find multirom folder!"
    exit 1
fi

mkdir "$path"
mkdir "$path/root"
cp /tmp/rom/rom_info.txt "$path/rom_info.txt"

echo "Extracting tar..."
/tmp/gnutar --numeric-owner -C "$path/root" -xf /tmp/rom/root.tar.gz
exit $?