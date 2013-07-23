#!/bin/sh
DEVICES=$1
SCRIPT_PATH=$2
CHECK_PROPS="ro.product.device ro.build.product"

if [ "$#" -ne "2" ]; then
    echo "Usage: $0 [device] [path to folder with updater-script]"
    exit 1
fi

# Some devices have multiple variants, which are almost the same,
# Example: grouper and tilapia (WiFi and 3G versions of Nexus 7)
case "$DEVICES" in
    "grouper")
        DEVICES="${DEVICES} tilapia"
        ;;
esac

fail()
{
    echo make_updater_script.sh has failed: $1
    exit 1
}

mv ${SCRIPT_PATH}/updater-script ${SCRIPT_PATH}/updater-script-base || fail "Failed to copy updater-script base"

assert_str="assert("
for dev in $DEVICES; do
    for prop in $CHECK_PROPS; do
        assert_str="${assert_str}getprop(\"$prop\") == \"$dev\" || "
    done
    assert_str="${assert_str}\n       "
done

assert_str="${assert_str% || \\n *});\n"

echo -e "$assert_str" > ${SCRIPT_PATH}/updater-script || fail "Failed to write assert line into updater-script!"
cat ${SCRIPT_PATH}/updater-script-base >> ${SCRIPT_PATH}/updater-script || fail "Failed to add base updater-script file!"
rm ${SCRIPT_PATH}/updater-script-base
