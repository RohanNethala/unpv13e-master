# UDP Client-Server Example ('hw2.c')

Authors: Peter Simone (simonp2) and Rohan Nethala (nethar)
Date: September 30, 2025

This project demonstrates a simple UDP client-server communication using C, based on the implementation in `hw2.c`.

## Files

- 'hw2.c': Contains both client and server code for UDP communication.
- 'README.txt': This file.
- 'Makefile': Sets up the environment and runs code.

## Features
- RRQ aka Read Request: Sends files from server to client in 512-byte blocks.
- WRQ aka Write Request: Receives files from client and stores them locally.
- Concurrency: Multiple clients are supported using 'fork()'.
- Timeouts & Retransmission:
  - Retransmit last packet if no response for 1 second.
  - Abort connection if no response for 10 seconds.
- Mode: Only "octet"/"binary" is supported (as required).
- Port Management:
  - Listens on the first port in the given range.
  - Assigns subsequent ports as transfer identifiers (TIDs).
- File Safety:
  - Rejects absolute/relative paths and ".." for security.
  - Rejects overwrite attempts if a file already exists.
  - Limits filename length for safety.
- Error Handling: Returns TFTP error packets for invalid requests, access violations, nonexistent files, disk full, etc.

