#!/bin/bash

#
#make
#fusermount -u ~/secret
./hsencfs -q -l 4 -p 1234 ~/.secret ~/secret
cp Makefile ~/secret
diff Makefile ~/secret/Makefile
fusermount -u ~/secret




