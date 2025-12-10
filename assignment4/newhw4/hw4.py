#!/usr/bin/env python3

from concurrent import futures
import sys
import socket
import threading
import time
from collections import defaultdict, deque
import grpc

import csci4220_hw4_pb2 as pb2
import csci4220_hw4_pb2_grpc as pb2_grpc

# Global lock for thread-safe printing
print_lock = threading.Lock()

class PeerInfo:
    """Simple class to represent peer information."""
    def __init__(self, node_id, address, port):
        self.node_id = node_id
        self.address = address
        self.port = port
    
    def __hash__(self):
        return hash((self.node_id, self.address, self.port))
    
    def __eq__(self, other):
        if not isinstance(other, PeerInfo):
            return False
        return (self.node_id == other.node_id and 
                self.address == other.address and 
                self.port == other.port)

def ConvertToProto(peer):
    """Convert PeerInfo to protobuf Node."""
    p = pb2.Node()
    p.id = peer.node_id
    p.address = peer.address
    p.port = peer.port
    return p

def ConvertFromProto(proto_node):
    """Convert protobuf Node to PeerInfo."""
    return PeerInfo(node_id=proto_node.id, address=proto_node.address, port=proto_node.port)

class DHTStore:
    """Local key-value store with TTL."""
    def __init__(self, ttl=86400.0):
        self._store = {}  # key stores (value, timestamp)
        self._lock = threading.Lock()
        self.ttl = ttl
    
    def StoreValue(self, key, value):
        """Store a key-value pair."""
        with self._lock:
            self._store[key] = (value, time.time())
    
    def FindValue(self, key):
        """Find a value by key, checking TTL."""
        with self._lock:
            if key not in self._store:
                return None
            value, timestamp = self._store[key]
            # Check if expired
            if time.time() - timestamp > self.ttl:
                del self._store[key]
                return None
            return value

class KBucket:
    """k-bucket with LRU semantics."""
    def __init__(self, k):
        self.k = k
        self.nodes = deque()  # deque of (node_id, address, port) tuples
    
    def AddNode(self, peer):
        """Add a node to the k-bucket with LRU semantics."""
        node_ids = [n[0] for n in self.nodes]
        
        # If node already exists, move to back (most recently used)
        if peer.node_id in node_ids:
            self.nodes = deque((n for n in self.nodes if n[0] != peer.node_id))
            self.nodes.append((peer.node_id, peer.address, peer.port))
        # If bucket not full, add to back
        elif len(self.nodes) < self.k:
            self.nodes.append((peer.node_id, peer.address, peer.port))
        else:
            # Bucket full, evict oldest (front) and add new to back
            self.nodes.popleft()
            self.nodes.append((peer.node_id, peer.address, peer.port))
    
    def RemoveNode(self, node_id):
        """Remove a node by ID."""
        self.nodes = deque((n for n in self.nodes if n[0] != node_id))
    
    def Touch(self, node_id):
        """Mark a node as recently used by moving to back."""
        for i, n in enumerate(self.nodes):
            if n[0] == node_id:
                node = self.nodes[i]
                temp = list(self.nodes)
                temp.remove(node)
                temp.append(node)
                self.nodes = deque(temp)
                break
    
    def GetNodes(self):
        """Get all nodes in the bucket as PeerInfo objects."""
        return [PeerInfo(n[0], n[1], n[2]) for n in self.nodes]
    
    def Contains(self, node_id):
        """Check if a node is in the bucket."""
        return any(n[0] == node_id for n in self.nodes)

class RoutingTable:
    """Routing table with N=4 k-buckets."""
    def __init__(self, our_id, k, num_buckets=4):
        self.our_id = our_id
        self.k = k
        self.num_buckets = num_buckets
        self.buckets = [KBucket(k) for _ in range(num_buckets)]
        self.lock = threading.Lock()
    
    def _GetBucketIndex(self, node_id):
        """Calculate bucket index based on XOR distance."""
        distance = node_id ^ self.our_id
        if distance == 0:
            return None  # Don't store self
        idx = distance.bit_length() - 1
        return min(idx, self.num_buckets - 1)
    
    def AddNode(self, peer):
        """Add a node to the appropriate bucket."""
        idx = self._GetBucketIndex(peer.node_id)
        if idx is None:
            return
        with self.lock:
            self.buckets[idx].AddNode(peer)
    
    def MarkNodeSeen(self, peer):
        """Mark a node as recently seen."""
        idx = self._GetBucketIndex(peer.node_id)
        if idx is None:
            return
        with self.lock:
            if any(n.node_id == peer.node_id for n in self.buckets[idx].GetNodes()):
                self.buckets[idx].Touch(peer.node_id)
    
    def FindKClosest(self, target_id):
        """Find k nodes closest to target_id."""
        with self.lock:
            all_nodes = []
            for bucket in self.buckets:
                all_nodes.extend(bucket.GetNodes())
        
        # Filter out self
        all_nodes = [n for n in all_nodes if n.node_id != self.our_id]
        
        # Sort by XOR distance
        all_nodes.sort(key=lambda n: n.node_id ^ target_id)
        return all_nodes[:self.k]
    
    def PrintBuckets(self):
        """Format k-buckets for output."""
        result = []
        with self.lock:
            for i in range(self.num_buckets):
                nodes = self.buckets[i].GetNodes()
                node_strs = [f"{n.node_id}:{n.port}" for n in nodes]
                result.append(f"{i}: {' '.join(node_strs)}")
        return '\n'.join(result)
    
    def RemoveNode(self, node_id):
        """Remove a node from routing table."""
        with self.lock:
            for i, bucket in enumerate(self.buckets):
                if any(n.node_id == node_id for n in bucket.GetNodes()):
                    bucket.RemoveNode(node_id)
                    return i
        return None

# RPC calling helpers
def SendFindNodeRPC(remote_peer, search_id, our_peer, timeout=5.0):
    """Call FindNode RPC on a remote node. Returns (responding_node, closest_nodes)."""
    try:
        channel = grpc.insecure_channel(f"{remote_peer.address}:{remote_peer.port}")
        stub = pb2_grpc.KadImplStub(channel)
        
        request = pb2.IDKey()
        request.node.CopyFrom(ConvertToProto(our_peer))
        request.idkey = search_id
        
        response = stub.FindNode(request, timeout=timeout)
        
        # Get the responding node's info
        responding_node = ConvertFromProto(response.responding_node)
        
        # Get the list of closest nodes
        nodes = [ConvertFromProto(node) for node in response.nodes]
        
        channel.close()
        return (responding_node, nodes)
    except Exception as e:
        return None

def SendFindValueRPC(remote_peer, search_key, our_peer, timeout=5.0):
    """Call FindValue RPC on a remote node."""
    try:
        channel = grpc.insecure_channel(f"{remote_peer.address}:{remote_peer.port}")
        stub = pb2_grpc.KadImplStub(channel)
        
        request = pb2.IDKey()
        request.node.CopyFrom(ConvertToProto(our_peer))
        request.idkey = search_key
        
        response = stub.FindValue(request, timeout=timeout)
        
        responding_node = ConvertFromProto(response.responding_node)
        
        if response.mode_kv:
            return (responding_node, response.kv.value, None)
        else:
            nodes = [ConvertFromProto(node) for node in response.nodes]
            return (responding_node, None, nodes)
    except Exception:
        return None

def SendStoreRPC(remote_peer, key, value, our_peer, timeout=5.0):
    """Call Store RPC on a remote node."""
    try:
        channel = grpc.insecure_channel(f"{remote_peer.address}:{remote_peer.port}")
        stub = pb2_grpc.KadImplStub(channel)
        
        request = pb2.KeyValue()
        request.node.CopyFrom(ConvertToProto(our_peer))
        request.key = key
        request.value = value
        
        stub.Store(request, timeout=timeout)
        channel.close()
        return True
    except Exception:
        return False

def SendQuitRPC(remote_peer, quitting_id, our_peer, timeout=5.0):
    """Call Quit RPC to notify a remote node."""
    try:
        channel = grpc.insecure_channel(f"{remote_peer.address}:{remote_peer.port}")
        stub = pb2_grpc.KadImplStub(channel)
        
        request = pb2.IDKey()
        request.node.CopyFrom(ConvertToProto(our_peer))
        request.idkey = quitting_id
        
        stub.Quit(request, timeout=timeout)
        channel.close()
        return True
    except Exception:
        return False

# gRPC Service Implementation
class KadServicer(pb2_grpc.KadImplServicer):
    def __init__(self, node_id, port, routing_table, dht_store, k):
        self.node_id = node_id
        self.port = port
        self.routing_table = routing_table
        self.dht_store = dht_store
        self.k = k
    
    def FindNode(self, request, context):
        """Handle FindNode RPC."""
        requesting_peer = ConvertFromProto(request.node)
        target_id = request.idkey
        
        # Log the request
        with print_lock:
            print(f"Serving FindNode({target_id}) request for {requesting_peer.node_id}")
            sys.stdout.flush()
        
        # Update routing table with requester
        self.routing_table.AddNode(requesting_peer)
        
        # Find k closest nodes to target_id
        with self.routing_table.lock:
            all_nodes = []
            for bucket in self.routing_table.buckets:
                all_nodes.extend(bucket.GetNodes())
        
        # Filter out self and requesting node
        all_nodes = [n for n in all_nodes if n.node_id != self.node_id and n.node_id != requesting_peer.node_id]
        
        # Sort by distance and take top k
        all_nodes.sort(key=lambda n: n.node_id ^ target_id)
        closest_nodes = all_nodes[:self.k]
        
        # Create response
        response = pb2.NodeList()
        response.responding_node.CopyFrom(ConvertToProto(
            PeerInfo(node_id=self.node_id, address="127.0.0.1", port=self.port)
        ))
        
        for node in closest_nodes:
            node_proto = response.nodes.add()
            node_proto.CopyFrom(ConvertToProto(node))
        
        return response
    
    def FindValue(self, request, context):
        """Handle FindValue RPC."""
        requesting_peer = ConvertFromProto(request.node)
        key = request.idkey
        
        # Log the request
        with print_lock:
            print(f"Serving FindKey({key}) request for {requesting_peer.node_id}")
            sys.stdout.flush()
        
        # Update routing table with requester
        self.routing_table.AddNode(requesting_peer)
        
        # Try to find the value
        value = self.dht_store.FindValue(key)
        
        response = pb2.KV_Node_Wrapper()
        response.responding_node.CopyFrom(ConvertToProto(
            PeerInfo(node_id=self.node_id, address="127.0.0.1", port=self.port)
        ))
        
        if value is not None:
            response.mode_kv = True
            response.kv.key = key
            response.kv.value = value
        else:
            response.mode_kv = False
            closest_nodes = self.routing_table.FindKClosest(key)
            for node in closest_nodes:
                node_proto = response.nodes.add()
                node_proto.CopyFrom(ConvertToProto(node))
        
        return response
    
    def Store(self, request, context):
        """Handle Store RPC."""
        requesting_peer = ConvertFromProto(request.node)
        
        # Update routing table with requester
        self.routing_table.AddNode(requesting_peer)
        
        # Store the key-value pair
        self.dht_store.StoreValue(request.key, request.value)
        
        # Log the store operation
        with print_lock:
            print(f"Storing key {request.key} value \"{request.value}\"")
            sys.stdout.flush()
        
        # Return our node ID
        response = pb2.IDKey()
        response.idkey = self.node_id
        
        return response
    
    def Quit(self, request, context):
        """Handle Quit RPC."""
        quitting_id = request.idkey
        
        # Remove the quitting node
        bucket_idx = self.routing_table.RemoveNode(quitting_id)
        
        # Log the quit
        with print_lock:
            if bucket_idx is not None:
                print(f"Evicting quitting node {quitting_id} from bucket {bucket_idx}")
            else:
                print(f"No record of quitting node {quitting_id} in k-buckets.")
            sys.stdout.flush()
        
        # Return our node ID
        response = pb2.IDKey()
        response.idkey = self.node_id
        
        return response

# Iterative lookup algorithms
def PerformIterativeFindNode(our_peer, target_id, routing_table, k):
    """Iterative FindNode lookup with serial queries."""
    if target_id == our_peer.node_id:
        return [our_peer]
    
    queried = set()
    closest = set()
    queried_nodes_list = []
    
    # Get initial candidates
    candidates = routing_table.FindKClosest(target_id)
    for node in candidates:
        closest.add(node)
    
    iterations = 0
    max_iterations = 10
    
    while iterations < max_iterations:
        iterations += 1
        
        # Sort current closest by distance
        sorted_closest = sorted(list(closest), key=lambda n: n.node_id ^ target_id)
        closest_k = sorted_closest[:k]
        
        # Find unqueried nodes
        unqueried = [n for n in closest_k if n.node_id not in queried and n.node_id != our_peer.node_id]
        
        if not unqueried:
            break
        
        # Query the first unqueried node (serial approach)
        node_to_query = unqueried[0]
        queried.add(node_to_query.node_id)
        queried_nodes_list.append(node_to_query)
        
        # Call FindNode on remote node
        result = SendFindNodeRPC(node_to_query, target_id, our_peer, timeout=5.0)
        
        if result is None:
            continue
        
        responding_node, discovered_nodes = result
        
        # Add discovered nodes
        for node in discovered_nodes:
            if node.node_id != our_peer.node_id:
                closest.add(node)
        
        # Check if we found the target
        for node in discovered_nodes:
            if node.node_id == target_id:
                # Mark queried nodes as seen
                for qnode in reversed(queried_nodes_list):
                    try:
                        routing_table.AddNode(qnode)
                        routing_table.MarkNodeSeen(qnode)
                    except Exception:
                        pass
                return [node]
    
    sorted_closest = sorted(list(closest), key=lambda n: n.node_id ^ target_id)
    result = sorted_closest[:k]
    
    # Mark queried nodes as seen
    for qnode in reversed(queried_nodes_list):
        try:
            routing_table.AddNode(qnode)
            routing_table.MarkNodeSeen(qnode)
        except Exception:
            pass
    
    return result

def PerformIterativeFindValue(our_peer, key, routing_table, k):
    """Iterative FindValue lookup."""
    # Initialize tracking structures
    queried_set = set()
    new_peers = set()
    contacted_successfully = []
    
    # Get starting peers from routing table
    starting_peers = routing_table.FindKClosest(key)
    
    # Calculate initial minimum distance
    min_dist = float('inf')
    if starting_peers:
        min_dist = min(p.node_id ^ key for p in starting_peers)
    
    # Query all starting peers
    for peer in starting_peers:
        if peer.node_id == our_peer.node_id:
            continue
        
        queried_set.add(peer.node_id)
        rpc_result = SendFindValueRPC(peer, key, our_peer, timeout=5.0)
        
        if rpc_result is None:
            continue
        
        responding_node, val, peer_list = rpc_result
        
        if val is not None:
            routing_table.AddNode(responding_node)
            routing_table.MarkNodeSeen(responding_node)
            return (val, None)
        
        contacted_successfully.append(responding_node)
        
        if peer_list:
            for p in peer_list:
                if p.node_id not in queried_set:
                    new_peers.add(p)
    
    # Update routing table with successfully contacted peers
    for peer in reversed(contacted_successfully):
        try:
            routing_table.AddNode(peer)
            routing_table.MarkNodeSeen(peer)
        except Exception:
            pass
    
    # Check if we should do a second query round
    if new_peers and starting_peers:
        sorted_new = sorted(new_peers, key=lambda p: p.node_id ^ key)
        closest_new = sorted_new[0]
        new_dist = closest_new.node_id ^ key
        
        # Determine bucket classifications
        start_bucket_idx = None
        if min_dist > 0:
            start_bucket_idx = min(min_dist.bit_length() - 1, 3)
        
        new_bucket_idx = None
        if new_dist > 0:
            new_bucket_idx = min(new_dist.bit_length() - 1, 3)
        
        # Decide whether to query
        do_query = (new_dist < min_dist and 
                   closest_new.node_id not in queried_set and 
                   closest_new.node_id != our_peer.node_id)
        
        if do_query and start_bucket_idx is not None and new_bucket_idx is not None:
            if start_bucket_idx == new_bucket_idx:
                do_query = False
        
        if do_query:
            queried_set.add(closest_new.node_id)
            rpc_result = SendFindValueRPC(closest_new, key, our_peer, timeout=5.0)
            
            if rpc_result is not None:
                responding_node, val, peer_list = rpc_result
                if val is not None:
                    routing_table.AddNode(responding_node)
                    routing_table.MarkNodeSeen(responding_node)
                    return (val, None)
                
                routing_table.AddNode(responding_node)
                routing_table.MarkNodeSeen(responding_node)
                
                if peer_list:
                    for p in peer_list:
                        if p.node_id not in queried_set:
                            new_peers.add(p)
    
    # Return k closest from all discovered peers
    combined = list(new_peers) + starting_peers
    filtered = [p for p in combined if p.node_id != our_peer.node_id]
    filtered.sort(key=lambda p: p.node_id ^ key)
    return (None, filtered[:k])

def run():
    """Main CLI loop."""
    if len(sys.argv) != 4:
        print(f"Error, correct usage is {sys.argv[0]} [my id] [my port] [k]")
        sys.exit(-1)
    
    local_id = int(sys.argv[1])
    my_port = int(sys.argv[2])
    k = int(sys.argv[3])
    
    my_hostname = socket.gethostname()
    my_address = socket.gethostbyname(my_hostname)
    
    local_peer = PeerInfo(node_id=local_id, address=my_address, port=my_port)
    dht_store = DHTStore()
    routing_table = RoutingTable(our_id=local_id, k=k, num_buckets=4)
    last_bootstrap_node = None
    
    # Start gRPC server
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    servicer = KadServicer(node_id=local_id, port=my_port, routing_table=routing_table, dht_store=dht_store, k=k)
    pb2_grpc.add_KadImplServicer_to_server(servicer, server)
    
    server_address = f"[::]:{my_port}"
    server.add_insecure_port(server_address)
    server.start()
    
    # Main command loop
    try:
        while True:
            try:
                user_input = input().strip()
                if not user_input:
                    continue
                
                parts = user_input.split()
                command = parts[0].upper()
                
                if command == "BOOTSTRAP":
                    remote_hostname = parts[1]
                    remote_port = int(parts[2])
                    remote_addr = socket.gethostbyname(remote_hostname)
                    
                    # Create a temporary node info with unknown ID (will be discovered)
                    temp_node = PeerInfo(node_id=-1, address=remote_addr, port=remote_port)
                    
                    # Call FindNode to discover the remote node's ID
                    result = SendFindNodeRPC(temp_node, local_id, local_peer, timeout=5.0)
                    
                    if result:
                        responding_node, closest_nodes = result
                        
                        # Add the bootstrap node to routing table
                        routing_table.AddNode(responding_node)
                        
                        # Add any other discovered nodes
                        for node in closest_nodes:
                            if node.node_id != local_id:
                                routing_table.AddNode(node)
                        
                        with print_lock:
                            print(f"After BOOTSTRAP({responding_node.node_id}), k-buckets are:")
                            print(routing_table.PrintBuckets())
                            sys.stdout.flush()
                        
                        last_bootstrap_node = responding_node
                    else:
                        with print_lock:
                            print(f"BOOTSTRAP failed - could not reach node")
                            sys.stdout.flush()
                
                elif command == "STORE":
                    key = int(parts[1])
                    value = " ".join(parts[2:])
                    
                    # Calculate distance from our node to the key
                    our_distance = local_peer.node_id ^ key
                    
                    # Find k closest nodes to this key
                    closest_nodes = routing_table.FindKClosest(key)
                    
                    if closest_nodes:
                        # Check if we are closer than the closest node
                        closest_distance = closest_nodes[0].node_id ^ key
                        
                        if our_distance <= closest_distance:
                            # We are the closest - store locally
                            dht_store.StoreValue(key, value)
                            with print_lock:
                                print(f"Storing key {key} at node {local_peer.node_id}")
                                sys.stdout.flush()
                        else:
                            # Remote node is closer
                            target_node = closest_nodes[0]
                            with print_lock:
                                print(f"Storing key {key} at node {target_node.node_id}")
                                sys.stdout.flush()
                            SendStoreRPC(target_node, key, value, local_peer, timeout=5.0)
                    else:
                        # No other nodes - store locally
                        dht_store.StoreValue(key, value)
                        with print_lock:
                            print(f"Storing key {key} at node {local_peer.node_id}")
                            sys.stdout.flush()
                
                elif command == "FIND_VALUE":
                    key = int(parts[1])
                    
                    with print_lock:
                        print(f"Before FIND_VALUE command, k-buckets are:")
                        print(routing_table.PrintBuckets())
                        sys.stdout.flush()
                    
                    # Check local storage first
                    local_value = dht_store.FindValue(key)
                    if local_value:
                        with print_lock:
                            print(f"Found data \"{local_value}\" for key {key}")
                            sys.stdout.flush()
                    else:
                        # Perform iterative find
                        value, closest_nodes = PerformIterativeFindValue(local_peer, key, routing_table, k)
                        if value:
                            with print_lock:
                                print(f"Found value \"{value}\" for key {key}")
                                sys.stdout.flush()
                        else:
                            with print_lock:
                                print(f"Could not find key {key}")
                                sys.stdout.flush()
                        
                        # Add discovered nodes to routing table
                        if closest_nodes:
                            for node in closest_nodes:
                                if node.node_id != local_peer.node_id:
                                    routing_table.AddNode(node)
                    
                    with print_lock:
                        print(f"After FIND_VALUE command, k-buckets are:")
                        print(routing_table.PrintBuckets())
                        sys.stdout.flush()
                
                elif command == "FIND_NODE":
                    target_id = int(parts[1])
                    
                    with print_lock:
                        print(f"Before FIND_NODE command, k-buckets are:")
                        print(routing_table.PrintBuckets())
                        sys.stdout.flush()
                    
                    # Check if we're looking for ourselves
                    if target_id == local_id:
                        with print_lock:
                            print(f"Found destination id {target_id}")
                            print(f"After FIND_NODE command, k-buckets are:")
                            print(routing_table.PrintBuckets())
                            sys.stdout.flush()
                        continue
                    
                    # Perform iterative find
                    closest_nodes = PerformIterativeFindNode(local_peer, target_id, routing_table, k)
                    
                    # Check if we found the target
                    found = any(node.node_id == target_id for node in closest_nodes)
                    
                    if found:
                        with print_lock:
                            print(f"Found destination id {target_id}")
                            sys.stdout.flush()
                    else:
                        with print_lock:
                            print(f"Could not find destination id {target_id}")
                            sys.stdout.flush()
                    
                    # Add discovered nodes to routing table
                    for node in closest_nodes:
                        if node.node_id != local_id:
                            routing_table.AddNode(node)
                    
                    with print_lock:
                        print(f"After FIND_NODE command, k-buckets are:")
                        print(routing_table.PrintBuckets())
                        sys.stdout.flush()
                
                elif command == "QUIT":
                    # Notify all known nodes
                    for bucket in routing_table.buckets:
                        for node in bucket.GetNodes():
                            with print_lock:
                                print(f"Letting {node.node_id} know I'm quitting.")
                                sys.stdout.flush()
                            SendQuitRPC(node, local_id, local_peer, timeout=1.0)
                    
                    with print_lock:
                        print(f"Shut down node {local_id}")
                        sys.stdout.flush()
                    
                    server.stop(0)
                    break
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                pass
    
    except KeyboardInterrupt:
        with print_lock:
            print(f"Shut down node {local_id}")
            sys.stdout.flush()
        server.stop(0)

if __name__ == '__main__':
    run()
