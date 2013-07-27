#!/sbin/sh
base=""
if [ -d "/data/media/multirom" ] ; then
    base="/data/media/multirom"
elif [ -d "/data/media/0/multirom" ] ; then
    base="/data/media/0/multirom"
else
    echo "MultiROM folder was not found"
    exit 0
fi

/tmp/busybox chattr -R -i "$base"
rm -r "$base"
return $?
