#!/bin/bash
cmd "/C dir /B H:\CD5" | grep -i -E ".iso|.bin" > scsifns.txt
