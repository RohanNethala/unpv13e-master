# Multi-Client Multi-Threaded Fibonacci Service ('lab.4')

Authors: Peter Simone (simonp2) and Rohan Nethala (nethar)
Date: October 7, 2025

This project demonstrates the implementation of a multi-client, multi-threaded TCP server which computes a Fibonacci sequence utilizing worker threads.

## Files

- 'lab4.c': Contains complete implementation including socket management, client management, and worker threads.
- 'README.txt': This file.

## Features
- Multi-client support: Server allow multiple TCP connections at once, each client has its own thread.
- Parallel Fibonacci computation: Divides the range of the given Fibonacci sequence evenly across worker threads for concurrent processing
- Result aggregation: Returns the partial sum of each worker thread and combines them all for a final result

## Usage
./lab4 <port> <num_threads>