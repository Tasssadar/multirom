#!/sbin/sh
MIN_VER="5"

path=""
if [ -e "/data/media/multirom/multirom" ] ; then
    path="/data/media/multirom/multirom"
elif [ -e "/data/media/0/multirom/multirom" ] ; then
    path="/data/media/0/multirom/multirom"
else
    echo "Failed to find multirom binary!" 1>&2
    exit 1
fi
echo "Checking MultiROM version..."

res=$($path -v)
if [ "$?" -ne "0" ] ; then 
    echo "Failed to execute MultiROM binary!" 1>&2
    exit 1
fi

echo "Got version $res"

if [ "$res" -lt "$MIN_VER" ] ; then
    echo "Your MultiROM version ($res) is too low, version $MIN_VER is required!" 1>&2
    exit 1
fi

exit 0
