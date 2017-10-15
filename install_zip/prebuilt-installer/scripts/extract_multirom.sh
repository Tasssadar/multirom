#!/sbin/sh
cont=""
base=""

# Move old installation of multirom to MultiROM/multirom
if [ -d "/data/media/multirom" ] && [ ! -d "/data/media/MultiROM/multirom" ]; then
    mkdir "/data/media/MultiROM"
    mv "/data/media/multirom" "/data/media/MultiROM/multirom"
elif [ -d "/data/media/0/multirom" ] && [ ! -d "/data/media/0/MultiROM/multirom" ]; then
    mkdir "/data/media/0/MultiROM"
    mv "/data/media/0/multirom" "/data/media/0/MultiROM/multirom"
fi

# Check for existing dirs otherwise create them
if [ -d "/data/media/MultiROM/multirom" ] ; then
    cont="/data/media/MultiROM"
    base="/data/media/MultiROM/multirom"
elif [ -d "/data/media/0/MultiROM/multirom" ] ; then
    cont="/data/media/0/MultiROM"
    base="/data/media/0/MultiROM/multirom"
else
    if [ -d "/data/media/0" ] ; then
        cont="/data/media/0/MultiROM"
        base="/data/media/0/MultiROM/multirom"
    else
        cont="/data/media/MultiROM"
        base="/data/media/MultiROM/multirom"
    fi

    mkdir "$cont"
    chown root:root "$cont"
    chmod 770 "$cont"

    mkdir "$base"
    chown root:root "$base"
    chmod 770 "$base"

    mkdir "$base/roms"
    chown media_rw:media_rw "$base/roms"
    chmod 777 "$base/roms"
fi

# Main installation
rm "$base/boot.img-ubuntu"*
rm "$base/infos/"*
rm "$base/res/"*
cp -r /tmp/multirom/* "$base/"
chmod 755 "$base/multirom"
chmod 755 "$base/busybox"
chmod 750 "$base/trampoline"
chmod 755 "$base/kexec"
chmod 755 "$base/ntfs-3g"
chmod 755 "$base/exfat-fuse"
chmod 755 "$base/lz4"
chmod 755 "$base/ubuntu-init/init"
chmod 644 "$base/ubuntu-init/local"
chmod 755 "$base/ubuntu-touch-init/init"
chmod 644 "$base/ubuntu-touch-init/scripts/touch"
chmod 755 "$base/ubuntu-touch-sysimage-init/init"
chmod 644 "$base/ubuntu-touch-sysimage-init/scripts/touch"

# Remove immutable flag so we can chmod and chown
chattr -i "$cont"

# This makes does not allows access for media scanner on android, but
# still is enough for ubuntu
chmod 770 "$cont"
chown root:root "$cont"
touch "$cont/.nomedia"
chown media_rw:media_rw "$cont/.nomedia"

chmod 770 "$base"
chown root:root "$base"
touch "$base/.nomedia"
chown media_rw:media_rw "$base/.nomedia"

# Make the parent/container folder immutable to prevent sdcardfs uid/gid derivation
chattr +i "$cont"
