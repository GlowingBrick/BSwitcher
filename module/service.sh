#!/system/bin/sh

wait_until_login() {        #来自uperf
    # in case of /data encryption is disabled
    while [ "$(getprop sys.boot_completed)" != "1" ]; do
        sleep 1
    done

    # we doesn't have the permission to rw "/sdcard" before the user unlocks the screen
    local test_file="/sdcard/Android/.TEST_YKS"
    true >"$test_file"
    while [ ! -f "$test_file" ]; do
        true >"$test_file"
        sleep 1
    done
    rm "$test_file"
}

wait_until_login
MODDIR=${0%/*}
cd $MODDIR
chmod +x ${MODDIR}/BSwitcher
chmod +x ${MODDIR}/aapt
killall BSwitcher
${MODDIR}/BSwitcher
