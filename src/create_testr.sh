#!/bin/bash

fusermount -u ~/secret >/dev/null 2>&1
../tools/bpenc2 -f -p 1234 test/testfile ~/.secret/testfile2
../tools/bpdec2 -f  -p 1234  ~/.secret/testfile2 test/testfile3
diff -s test/testfile test/testfile3






