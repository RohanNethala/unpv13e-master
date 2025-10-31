#ifndef COMMON_H
#define COMMON_H

// -----------------------------------------------------------------------------
// Standard headers
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

// -----------------------------------------------------------------------------
// Router simulation constants
// -----------------------------------------------------------------------------
#define MAX_NEIGH 16          // Maximum number of directly connected neighbors
#define MAX_DEST  128         // Maximum number of routing table entries
#define MAX_LINE  256         // Maximum length for one config file line

#define INF_COST 65535        // "Infinity" cost (unreachable route)
#define UPDATE_INTERVAL_SEC 5 // Periodic routing update interval (seconds)
#define DEAD_INTERVAL_SEC 15  // Time to mark neighbor dead if no updates
#define DATA_PORT_OFFSET 1000 // Data sockets use (control_port + offset)

// -----------------------------------------------------------------------------
// Message type identifiers
// -----------------------------------------------------------------------------
enum { MSG_DV = 2, MSG_DATA = 3 };

// -----------------------------------------------------------------------------
// Notes about #pragma pack(push,1) / #pragma pack(pop)
// -----------------------------------------------------------------------------
// Normally, C compilers insert *padding bytes* inside structs so that fields
// are aligned for faster memory access. For example, after a uint8_t field,
// the compiler might add 1–3 unused bytes before the next uint32_t field.
//
// But when we send structs directly across the network (with sendto/recvfrom),
// those padding bytes would make the binary layout unpredictable and different
// across machines.
//
// The directives below disable that padding *only for the following structs*:
//
//     #pragma pack(push,1)   --> save current alignment, then force 1-byte alignment
//     #pragma pack(pop)      --> restore the previous (default) alignment
//
// This ensures that each field in dv_msg_t and data_msg_t appears in memory
// exactly in the order and size declared — no hidden gaps — so the bytes on the
// wire are consistent for all routers and architectures.
// -----------------------------------------------------------------------------
#pragma pack(push,1)

// -----------------------------------------------------------------------------
// Distance Vector message format (sent between routers)
// -----------------------------------------------------------------------------
// Each router sends its entire routing table (or subset) to neighbors.
// Fields are in "network byte order" (NBO) so they can be sent over UDP.
//
// Example structure of a DV packet:
//
//   +------+------------+------+--------------------------+
//   |type=2|sender_id   |num   | [entries ...]            |
//   +------+------------+------+--------------------------+
//
// Each entry contains:
//   - destination network
//   - subnet mask
//   - path cost
//
typedef struct {
    uint8_t  type;       // Always MSG_DV for distance vector messages
    uint16_t sender_id;  // Router ID of sender (not IP)
    uint16_t num;        // Number of entries below
    struct {
        uint32_t net;    // Destination network (NBO)
        uint32_t mask;   // Subnet mask (NBO)
        uint16_t cost;   // Cost metric to reach that network (NBO)
    } e[MAX_DEST];
} dv_msg_t;

// -----------------------------------------------------------------------------
// Data packet format (forwarded between routers)
// -----------------------------------------------------------------------------
// This message simulates user data that the router must forward based on
// the routing table, i.e., ip packet.  It contains a TTL field (time-to-live) and small payload.
//
// Example:
//
//   +------+-----+-------------+-------------+------+----------------+
//   |type=3|ttl  |src_ip       |dst_ip       |len   |payload[...]    |
//   +------+-----+-------------+-------------+------+----------------+
//
typedef struct {
    uint8_t  type;       // Always MSG_DATA for data packets
    uint8_t  ttl;        // Time-to-live: decremented on each hop
    uint32_t src_ip;     // Source IP (NBO)
    uint32_t dst_ip;     // Destination IP (NBO)
    uint16_t payload_len;
    char     payload[128];
} data_msg_t;
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Neighbor state: information about directly connected routers
// -----------------------------------------------------------------------------
typedef struct {
    uint32_t ip;         // Neighbor's IP address (NBO, loopback used here)
    uint16_t ctrl_port;  // UDP port used for control messages (DV)
    uint16_t cost;       // Link cost to this neighbor
    time_t   last_heard; // Last time a DV was received
    bool     alive;      // True if neighbor is still reachable
} neighbor_t;

// -----------------------------------------------------------------------------
// Routing table entry
// -----------------------------------------------------------------------------
typedef struct {
    uint32_t dest_net;   // Destination network (NBO)
    uint32_t mask;       // Subnet mask (NBO)
    uint32_t next_hop;   // Next hop IP (0 for directly connected networks)
    char     iface[8];   // Optional interface name string
    uint16_t cost;       // Path cost metric (0 = local, 1+ = learned)
    time_t   last_update;// Timestamp of last DV update for this route
} route_entry_t;

// -----------------------------------------------------------------------------
// Router control block: represents one running router instance
// -----------------------------------------------------------------------------
typedef struct {
    uint16_t self_id;          // Unique router ID (from config)
    uint32_t self_ip;          // Router's own IP (NBO)
    uint16_t ctrl_port;        // UDP port for DV control messages

    int sock_ctrl;             // Socket for control (DV) messages
    int sock_data;             // Socket for data packets

    int num_neighbors;         // Number of directly connected neighbors
    neighbor_t neighbors[MAX_NEIGH];

    int num_routes;            // Number of entries in routing table
    route_entry_t routes[MAX_DEST];
} router_t;

// -----------------------------------------------------------------------------
// Helper functions used by both student and instructor code
// -----------------------------------------------------------------------------

// Print an error and exit program
static inline void die(const char* fmt, ...){
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

// Compute the data socket port from the control socket port.
// (For router on port 12000, its data socket is 13000.)
static inline uint16_t get_data_port(uint16_t ctrl){
    return (uint16_t)(ctrl + DATA_PORT_OFFSET);
}


// -----------------------------------------------------------------------------
// Find an existing (net,mask) entry or add a new one to the routing table.
// Used when receiving new DV entries.
// -----------------------------------------------------------------------------
static inline route_entry_t* rt_find_or_add(router_t* r, uint32_t net, uint32_t mask){
    for (int i = 0; i < r->num_routes; i++)
        if (r->routes[i].dest_net == net && r->routes[i].mask == mask)
            return &r->routes[i];

    if (r->num_routes >= MAX_DEST) return NULL;

    r->routes[r->num_routes] = (route_entry_t){
        .dest_net = net,
        .mask = mask,
        .next_hop = 0,
        .iface = "",
        .cost = INF_COST,
        .last_update = time(NULL)
    };
    return &r->routes[r->num_routes++];
}

// -----------------------------------------------------------------------------
// Perform Longest Prefix Match (LPM) lookup for a destination IP.
// Returns the best route entry or NULL if no match.
// -----------------------------------------------------------------------------
static inline route_entry_t* rt_lookup(router_t* r, uint32_t dst){
    route_entry_t* best = NULL;
    uint32_t best_mask = 0;

    for (int i = 0; i < r->num_routes; i++) {
        route_entry_t* e = &r->routes[i];
        // Check if destination belongs to this subnet
        if ((dst & e->mask) == (e->dest_net & e->mask)) {
            // Choose the entry with the longest mask (most specific)
            if (ntohl(e->mask) > ntohl(best_mask)) {
                best = e;
                best_mask = e->mask;
            }
        }
    }
    return best;
}

// -----------------------------------------------------------------------------
// Utility: convert a network-byte-order IP to dotted string
// -----------------------------------------------------------------------------
static inline const char* ipstr(uint32_t nbo, char* buf, size_t n){
    struct in_addr a; a.s_addr = nbo;
    const char* s = inet_ntoa(a);
    snprintf(buf, n, "%s", s ? s : "0.0.0.0");
    return buf;
}

// -----------------------------------------------------------------------------
// Print current routing table with a header
// Example:
//   [R1] ROUTES (dv-update):
//     network         mask            next_hop        cost
//     192.168.1.0     255.255.255.0   0.0.0.0         0
// -----------------------------------------------------------------------------
static inline void log_table(router_t* r, const char* why){
    printf("[R%u] ROUTES (%s):\n", r->self_id, why);
    printf("  %-15s %-15s %-15s %-5s\n", "network", "mask", "next_hop", "cost");

    for (int i = 0; i < r->num_routes; i++) {
        char n1[32], n2[32], n3[32];
        printf("  %-15s %-15s %-15s %-5u\n",
               ipstr(r->routes[i].dest_net, n1, sizeof(n1)),
               ipstr(r->routes[i].mask, n2, sizeof(n2)),
               ipstr(r->routes[i].next_hop, n3, sizeof(n3)),
               r->routes[i].cost);
    }
    fflush(stdout);
}

#endif // COMMON_H