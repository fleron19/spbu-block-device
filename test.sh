#!/bin/bash

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[1;36m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

DEV="/dev/block-device"
SIZE=$((4 * 1024 * 1024))
KSRC="src"

ok()    { echo -e "  ${GREEN}PASS${NC}  $1"; ran=$((ran + 1)); }
fail()  { echo -e "  ${RED}FAIL${NC}  $1"; }
passes() { echo -e "${CYAN}${BOLD}=== $ran/$tot passed ===${NC}"; }


cleanup() {
    if [ -b "$DEV" ]; then
        dd if=/dev/zero of="$DEV" bs=4k count=1 2>/dev/null || true
    fi
}

build_and_load() {
    echo -e "${BOLD}--- build & load${NC}"
    sudo rmmod block-device 2>/dev/null || true
    make -s -C "$KSRC" clean 2>/dev/null || true
    make -s -C "$KSRC"
    sudo insmod "$KSRC/block-device.ko"
    sleep 0.5
    if [ ! -b "$DEV" ]; then
        echo -e "  ${RED}FAIL${NC}  device node $DEV not found"
        exit 1
    fi
    echo -e "  ${GREEN}PASS${NC}  module loaded, device present"
}

unload() {
    echo -e "${BOLD}--- unload${NC}"
    sudo rmmod block-device 2>/dev/null || true
    echo -e "  ${GREEN}PASS${NC}  module unloaded"
}

ran=$((0)); tot=$((0))
run() {
    local name="$1"; shift
    tot=$((tot + 1))
    if "$@"; then
        ok "$name"
    else
        fail "$name"
    fi
}

test_device_exists() {
    [ -b "$DEV" ]
}

test_device_size() {
    local sz
    sz=$(sudo blockdev --getsize64 "$DEV")
    [ "$sz" -eq "$SIZE" ]
}

test_read_write() {
    local tmp=$(mktemp)
    dd if=/dev/urandom of="$tmp" bs=4k count=256 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k count=256 2>/dev/null
    local tmp2=$(mktemp)
    sudo dd if="$DEV" of="$tmp2" bs=4k count=256 2>/dev/null
    cmp "$tmp" "$tmp2"; local rc=$?
    rm -f "$tmp" "$tmp2"
    return $rc
}

test_write_read_full() {
    local tmp=$(mktemp)
    dd if=/dev/urandom of="$tmp" bs=1M count=4 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=1M count=4 2>/dev/null
    local tmp2=$(mktemp)
    sudo dd if="$DEV" of="$tmp2" bs=1M count=4 2>/dev/null
    cmp "$tmp" "$tmp2"; local rc=$?
    rm -f "$tmp" "$tmp2"
    return $rc
}

test_partial_read() {
    local tmp=$(mktemp)
    dd if=/dev/urandom of="$tmp" bs=4k count=1 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k count=1 seek=100 2>/dev/null
    local tmp2=$(mktemp)
    sudo dd if="$DEV" of="$tmp2" bs=4k count=1 skip=100 2>/dev/null
    cmp "$tmp" "$tmp2"; local rc=$?
    rm -f "$tmp" "$tmp2"
    return $rc
}

test_o_direct() {
    local tmp=$(mktemp)
    dd if=/dev/urandom of="$tmp" bs=4k count=64 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k count=64 oflag=direct 2>/dev/null
    local tmp2=$(mktemp)
    sudo dd if="$DEV" of="$tmp2" bs=4k count=64 iflag=direct 2>/dev/null
    cmp "$tmp" "$tmp2"; local rc=$?
    rm -f "$tmp" "$tmp2"
    return $rc
}

test_boundary_oob() {
    local tmp=$(mktemp)
    dd if=/dev/zero of="$tmp" bs=4k count=1 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k seek=1024 2>/dev/null
    local rc=$?; rm -f "$tmp"
    [ $rc -ne 0 ]
}

test_boundary_exact_end() {
    local tmp=$(mktemp)
    dd if=/dev/zero of="$tmp" bs=4k count=1 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k seek=1023 2>/dev/null
    local rc=$?; rm -f "$tmp"
    [ $rc -eq 0 ]
}

test_zero_pattern() {
	local tmp=$(mktemp)
	local zero=$(mktemp)
	local rc
	dd if=/dev/zero of="$zero" bs=1M count=4 2>/dev/null
	sudo dd if="$DEV" of="$tmp" bs=1M count=4 2>/dev/null
	cmp "$zero" "$tmp" 2>&1; rc=$?
	rm -f "$tmp" "$zero"
	return $rc
}

test_multi_block_sizes() {
    for bs in 512 1k 4k 8k 16k 32k 64k 128k; do
        local tmp=$(mktemp)
        dd if=/dev/urandom of="$tmp" bs=$bs count=4 2>/dev/null
        sudo dd if="$tmp" of="$DEV" bs=$bs count=4 2>/dev/null
        local tmp2=$(mktemp)
        sudo dd if="$DEV" of="$tmp2" bs=$bs count=4 2>/dev/null
        cmp "$tmp" "$tmp2" || { rm -f "$tmp" "$tmp2"; return 1; }
        rm -f "$tmp" "$tmp2"
    done
    return 0
}

test_concurrent_read_write() {
    local tmp=$(mktemp)
    dd if=/dev/urandom of="$tmp" bs=1M count=1 2>/dev/null
    sudo dd if="$tmp" of="$DEV" bs=4k seek=0 2>/dev/null &
    pid1=$!
    sudo dd if="$DEV" of=/dev/null bs=4k count=256 2>/dev/null &
    pid2=$!
    wait $pid1; local rc1=$?
    wait $pid2; local rc2=$?
    rm -f "$tmp"
    [ $rc1 -eq 0 ] && [ $rc2 -eq 0 ]
}

echo -e "${CYAN}${BOLD}=== block-device tests ===${NC}"
echo

cleanup
build_and_load

run "device exists"               test_device_exists
run "device size 4MiB"            test_device_size
run "read-back zeroes"            test_zero_pattern
run "write & read 1MiB"           test_read_write
run "write & read full 4MiB"      test_write_read_full
run "partial write & read"        test_partial_read
run "O_DIRECT write & read"       test_o_direct
run "multi block sizes"           test_multi_block_sizes
run "boundary OOB rejected"       test_boundary_oob
run "boundary exact end"          test_boundary_exact_end
run "concurrent read+write"       test_concurrent_read_write

unload

echo
passes
