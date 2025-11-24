#!/usr/bin/env python3

from concurrent import futures
import sys
import socket
import grpc
from dataclasses import dataclass

import threading
import time
from typing import Optional

import csci4220_hw4_pb2
import csci4220_hw4_pb2_grpc

# Here we make a node class to describe each node in the DHT
@dataclass(frozen=True)
class NodeInfo:
    node_id: int
    address: str
    port: int

    # This function will allow you to use NodeInfo in sets and as dict keys
    def __hash__(self):
        return hash(self.node_id)

    # This function allows you to compare a NodeInfo object to another one
    def __eq__(self, other):
        if isinstance(other, NodeInfo):
            return self.node_id == other.node_id
        return False


# This function will convert a NodeInfo object to a protobuf Node message
def node_to_proto(node: NodeInfo) -> csci4220_hw4_pb2.Node:
    return csci4220_hw4_pb2.Node(id=node.node_id, port=node.port, address=node.address)

# This function will convert a protobuf Node message to a NodeInfo object
def proto_to_node(proto_node: csci4220_hw4_pb2.Node) -> NodeInfo:
    return NodeInfo(node_id=proto_node.id, address=proto_node.address, port=proto_node.port)

# This class is for the DHTEntry objects that are stored in DHTStore
@dataclass
class DHTEntry:
    key: int
    value: str
    timestamp: float


# This class is for the DHTStore that holds key-value pairs with TTL
class DHTStore:
    
    # Here we initialize DHTStore object with a dictionary and ttl
    def __init__(self, ttl: float = 86400.0):
        self._store: dict[int, DHTEntry] = {}
        self._lock = threading.Lock()
        self.ttl = ttl
    
    # Here we define the store method to add key-value pairs
    def store(self, key: int, value: str):
        with self._lock:
            self._store[key] = DHTEntry(key=key, value=value, timestamp=time.time())
    
    # Here we define the find method to retrieve values by key
    def find(self, key: int) -> Optional[str]:
        with self._lock:
            if key not in self._store:
                return None
            entry = self._store[key]
            # Now we check if expired
            if time.time() - entry.timestamp > self.ttl:
                del self._store[key]
                return None
            return entry.value


def run():
    if len(sys.argv) != 4:
        print("Error, correct usage is {} [my id] [my port] [k]".format(sys.argv[0]))
        sys.exit(-1)

    local_id = int(sys.argv[1])
    my_port_str = str(int(sys.argv[2]))
    k = int(sys.argv[3])
    
    my_hostname = socket.gethostname()
    my_address = socket.gethostbyname(my_hostname)
    
    local_node = NodeInfo(node_id=local_id, address=my_address, port=int(my_port_str))
    dht_store = DHTStore()
    
    print(f"Node initialized: ID={local_node.node_id}, Address={local_node.address}, Port={local_node.port}, K={k}")
    print(f"DHTStore created and ready to store key-value pairs")


if __name__ == '__main__':
    run()
