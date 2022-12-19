#!/bin/bash

nodes=$1
python3 latin_square.py ${nodes} | sort -n | tail -n ${nodes} > alltoall_schedule_${nodes}.txt
