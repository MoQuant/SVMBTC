#!/bin/bash
echo "Compiling"
g++ -o btc bitcoin.cpp -std=c++11 -I/Library/Frameworks/Python.framework/Versions/3.11/include/python3.11 -L/Library/Frameworks/Python.framework/Versions/3.11/lib -lpython3.11 -lcpprest -lcrypto -lssl -lpthread
echo "Compiled"
exit 0