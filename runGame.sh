#!/bin/bash
set -e

g++ -std=c++11 -DLOCAL MyBot.cpp -o MyBot.o
g++ -std=c++11 RandomBot.cpp -o RandomBot.o
./halite -d "30 30" "./MyBot.o" "./RandomBot.o"
