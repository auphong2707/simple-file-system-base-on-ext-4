#!/bin/bash

# Function to measure time
measure_time() {
    START_TIME=$(date +%s.%N)
    "$@"
    END_TIME=$(date +%s.%N)
    DURATION=$(echo "$END_TIME - $START_TIME" | bc)
    echo "$DURATION seconds"
}

# Test 1: Creating 3000 directories
echo "TEST 1: Creating 3000 directories..."
CREATE_DIR_TIME=$(measure_time bash -c '
for i in {1..3000}; do
    mkdir "dir_$i"
done
')
echo "Time taken to create 3000 directories: $CREATE_DIR_TIME"

# Test 2: Deleting 3000 directories
echo -e "\nTEST 2: Deleting 3000 directories..."
DELETE_DIR_TIME=$(measure_time bash -c '
for i in {1..3000}; do
    rmdir "dir_$i"
done
')
echo "Time taken to delete 3000 directories: $DELETE_DIR_TIME"

# Test 3: Creating 100 files with 1MB data
echo -e "\nTEST 3: Creating 100 files with 1MB data..."
CREATE_FILE_TIME=$(measure_time bash -c '
for i in {1..100}; do
    dd if=/dev/urandom of="file_$i" bs=1M count=1 status=none
done
')
echo "Time taken to create 100 files with 1MB data: $CREATE_FILE_TIME"

# Test 4: Reading 100 files
echo -e "\nTEST 4: Reading 100 files..."
READ_FILE_TIME=$(measure_time bash -c '
for i in {1..100}; do
    cat "file_$i" > /dev/null
done
')
echo "Time taken to read 100 files with 1MB data: $READ_FILE_TIME"

# Test 5: Deleting 100 files
echo -e "\nTEST 5: Deleting 100 files..."
DELETE_FILE_TIME=$(measure_time bash -c '
for i in {1..100}; do
    rm "file_$i"
done
')
echo "Time taken to delete 100 files with 1MB data: $DELETE_FILE_TIME"

