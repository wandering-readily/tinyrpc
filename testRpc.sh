#!/bin/bash

count=0
while ((count < 10000)) ; do
    ./build/test_tinypb_server_client
done