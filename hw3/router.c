#include "common.h"

/*
 * CSCI-4220: Router Simulation (Distance Vector Routing)
 * ------------------------------------------------------
 * Starter Code for Students
 *
 * You will complete the Distance Vector (DV) routing logic.
 * The provided framework includes:
 *  - Configuration parsing (router_id, self_ip, neighbors, routes)
 *  - UDP socket setup (control + data)
 *  - Main event loop with select() timeout handling
 *
 * You will implement:
 *  - Distance Vector updates (Bellman-Ford)
 *  - Split Horizon and Poison Reverse in send_dv()
 *  - Packet forwarding using Longest Prefix Match (LPM)
 *  - Neighbor timeout handling
 */

/* =========================================================================
 * LOG MESSAGE SPECIFICATION
 * =========================================================================
 * Each router must print log messages to stdout showing its actions.
 * The format of each message is defined below and must match exactly.
 *
 * -------------------------------------------------------------------------
 * 1. Initialization
 * -------------------------------------------------------------------------
 * Printed once at startup to confirm routes loaded from configuration.
 * log_table(&R, "init");
 * Example:
 *   [R1] ROUTES (init):
 *     network         mask            next_hop        cost
 *     192.168.10.0    255.255.255.0   0.0.0.0         0
 *
 * -------------------------------------------------------------------------
 * 2. DV Update
 * -------------------------------------------------------------------------
 * Printed whenever the routing table changes after processing a DV message.
 * log_table(&R, "dv-update");
 * Example:
 *   [R2] ROUTES (dv-update):
 *     network         mask            next_hop        cost
 *     10.0.20.0       255.255.255.0   0.0.0.0         0
 *     192.168.10.0    255.255.255.0   127.0.1.1       1
 *     10.0.30.0       255.255.255.0   127.0.1.3       1
 *
 * -------------------------------------------------------------------------
 * 3. Neighbor Timeout
 * -------------------------------------------------------------------------
 * Printed when no DV updates are received from a neighbor for 15 seconds.
 * All routes learned from that neighbor should be poisoned (cost=65535).
 * log_table(&R, "neighbor-dead");
 * Example:
 *   [R1] ROUTES (neighbor-dead):
 *     network         mask            next_hop        cost
 *     10.0.20.0       255.255.255.0   127.0.1.2       65535
 *     10.0.30.0       255.255.255.0   127.0.1.2       65535
 *
 * -------------------------------------------------------------------------
 * 4. Packet Forwarding
 * -------------------------------------------------------------------------
 * Printed when the router forwards a data packet to the next hop. 
 * Code example: printf("[R%u] FWD dst=%s via=%s cost=%u ttl=%u\n", ...);
 *
 * Example:
 *   [R1] FWD dst=10.0.30.55 via=127.0.1.2 cost=2 ttl=7
 *
 * -------------------------------------------------------------------------
 * 5. Packet Delivery (To Self)
 * -------------------------------------------------------------------------
 * Printed when a packet reaches its destination router.
 *
 * Example:
 *   [R3] DELIVER self src=192.168.10.10 ttl=6 payload="hello LPM world"
 *
 * -------------------------------------------------------------------------
 * 6. Connected Delivery (No Next Hop)
 * -------------------------------------------------------------------------
 * Printed when delivering to a directly connected host.
 *
 * Example:
 *   [R1] DELIVER connected dst=192.168.10.55 payload="local test"
 *
 * -------------------------------------------------------------------------
 * 7. TTL Expired
 * -------------------------------------------------------------------------
 * Printed when a packet’s TTL reaches zero before delivery.
 *
 * Example:
 *   [R2] DROP ttl=0
 *
 * -------------------------------------------------------------------------
 * 8. Next Hop Down
 * -------------------------------------------------------------------------
 * Printed when forwarding is attempted but the next hop is marked dead.
 *
 * Example:
 *   [R1] NEXT HOP DOWN 127.0.1.2
 *
 * -------------------------------------------------------------------------
 * 9. No Matching Route
 * -------------------------------------------------------------------------
 * Printed when no route matches the packet’s destination (LPM lookup fails).
 *
 * Example:
 *   [R1] NO MATCH dst=10.0.99.55
 *
 * -------------------------------------------------------------------------
 * ✅ Summary
 * -------------------------------------------------------------------------
 * | Event              | Tag               | Example                                    |
 * |--------------------|-------------------|--------------------------------------------|
 * | Initialization     | (init)            | [R1] ROUTES (init): ...                    |
 * | DV Update          | (dv-update)       | [R2] ROUTES (dv-update): ...               |
 * | Neighbor Timeout   | (neighbor-dead)   | [R1] ROUTES (neighbor-dead): ...           |
 * | Packet Forward     | FWD               | [R1] FWD dst=...                           |
 * | Deliver to Self    | DELIVER self      | [R3] DELIVER self ...                      |
 * | Deliver Connected  | DELIVER connected | [R1] DELIVER connected ...                 |
 * | TTL Expired        | DROP ttl=0        | [R2] DROP ttl=0                            |
 * | Next Hop Down      | NEXT HOP DOWN     | [R1] NEXT HOP DOWN ...                     |
 * | No Route           | NO MATCH          | [R1] NO MATCH dst=...                      |
 *
 * -------------------------------------------------------------------------
 * Notes:
 * - All routers must prefix logs with [R#] where # = router_id.
 * - Logs are printed to stdout (not stderr).
 * - Field order and spacing must match examples for grading.
 * - Costs use 65535 (INF_COST) when poisoned.
 * =========================================================================
 */


static void trim(char* s){
    size_t n = strlen(s);
    while(n && isspace((unsigned char)s[n-1])) s[--n]=0;
}

/* -------------------------------------------------------------------------
 * Parse router configuration file (router_id, self_ip, routes, neighbors)
 * ------------------------------------------------------------------------- */
static void parse_conf(router_t* R, const char* path){
    FILE* f=fopen(path,"r");
    if(!f) die("open %s: %s", path, strerror(errno));

    char line[MAX_LINE];
    bool in_routes=false, in_neigh=false;

    while(fgets(line,sizeof(line),f)){
        trim(line);
        if(!line[0] || line[0]=='#') continue;

        if(!strncmp(line,"router_id",9)){
            int v; sscanf(line,"router_id %d",&v);
            R->self_id=v; continue;
        }

        if(!strncmp(line,"self_ip",7)){
            char ip[64]; sscanf(line,"self_ip %63s", ip);
            struct in_addr a; if(!inet_aton(ip,&a)) die("bad self_ip");
            R->self_ip=a.s_addr; continue;
        }

        if(!strncmp(line,"listen_port",11)){
            int p; sscanf(line,"listen_port %d",&p);
            R->ctrl_port=(uint16_t)p; continue;
        }

        if(!strncmp(line,"routes",6)){ in_routes=true; in_neigh=false; continue; }
        if(!strncmp(line,"neighbors",9)){ in_neigh=true; in_routes=false; continue; }

        if(in_routes){
            char net[64], mask[64], nh[64], ifn[16];
            if(sscanf(line,"%63s %63s %63s %15s", net, mask, nh, ifn)==4){
                struct in_addr a1,a2,a3;
                if(!inet_aton(net,&a1)||!inet_aton(mask,&a2)||!inet_aton(nh,&a3))
                    die("bad route line: %s", line);
                route_entry_t* e=rt_find_or_add(R,a1.s_addr,a2.s_addr);
                e->next_hop = a3.s_addr;
                e->cost = (a3.s_addr==0)?0:1;  // cost=0 for connected network
                snprintf(e->iface,sizeof(e->iface),"%s",ifn);
                e->last_update = time(NULL);
            }
        } else if(in_neigh){
            char ip[64]; int port, cost;
            if(sscanf(line,"%63s %d %d", ip, &port, &cost)==3){
                struct in_addr a; if(!inet_aton(ip,&a)) die("bad neighbor ip");
                if(R->num_neighbors>=MAX_NEIGH) die("too many neighbors");
                neighbor_t* nb = &R->neighbors[R->num_neighbors++];
                *nb = (neighbor_t){ .ip=a.s_addr, .ctrl_port=(uint16_t)port,
                                    .cost=(uint16_t)cost, .last_heard=time(NULL),
                                    .alive=true };
            }
        }
    }

    fclose(f);
    if(!R->self_ip || !R->ctrl_port)
        die("missing self_ip or listen_port");
}

/* -------------------------------------------------------------------------
 * Create and bind a UDP socket on the given port.
 * You may reuse this helper for control and data sockets.
 * ------------------------------------------------------------------------- */
static inline int udp_bind(uint16_t p){
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) die("socket: %s", strerror(errno));

    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(p);

    if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0)
        die("bind %u: %s", p, strerror(errno));
    return s;
}

/* -------------------------------------------------------------------------
 * TODO #1: Send Distance Vector update to a single neighbor
 *    - Fill dv_msg_t with routes and costs
 *    - Apply Split Horizon + Poison Reverse logic
 *    - Use sendto() to transmit the message
 * ------------------------------------------------------------------------- */
static void send_dv(router_t* R, const neighbor_t* nb){
    dv_msg_t msg = {0};
    msg.type = MSG_DV;
    msg.sender_id = R->self_id;
    msg.num = 0;

    // Fill the DV message entries
    for (int i = 0; i < R->num_routes; i++) {
        route_entry_t* e = &R->routes[i];
        msg.e[msg.num].net = e->dest_net;
        msg.e[msg.num].mask = e->mask;

        // Split Horizon / Poison Reverse
        if (e->next_hop == nb->ip && e->cost != 0)
            msg.e[msg.num].cost = INF_COST;
        else
            msg.e[msg.num].cost = e->cost;

        msg.num++;
    }

    // 🟢 Debug print before sending
    struct in_addr ip;
    ip.s_addr = nb->ip;
    printf("[R%u] sending DV to %s:%u with %u entries\n",
           R->self_id,
           inet_ntoa(ip),
           nb->ctrl_port,
           msg.num);

    // Prepare destination address
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(nb->ctrl_port);
    dest.sin_addr.s_addr = nb->ip;

    ssize_t n = sendto(R->sock_ctrl, &msg, sizeof(dv_msg_t), 0,
                       (struct sockaddr*)&dest, sizeof(dest));

    // 🟢 Check for errors or success
    if (n < 0)
        perror("sendto error");
    else
        printf("[R%u] DV sent successfully (%zd bytes)\n", R->self_id, n);
}

/* -------------------------------------------------------------------------
 * TODO #2: Broadcast DV updates to all alive neighbors
 * ------------------------------------------------------------------------- */
static void broadcast_dv(router_t* R){
    // TODO: Loop over neighbors and call send_dv() for each alive neighbor
    for (int i = 0; i < R->num_neighbors; i++) {
        neighbor_t* nb = &R->neighbors[i];

        if (!nb->alive) continue;

        // 🟢 Debug print to confirm broadcast is working
        struct in_addr ip;
        ip.s_addr = nb->ip;
        printf("[R%u] broadcasting DV to neighbor %s:%u (cost=%u)\n",
               R->self_id,
               inet_ntoa(ip),
               nb->ctrl_port,
               nb->cost);

        send_dv(R, nb);
    }
}

/* -------------------------------------------------------------------------
 * TODO #3: Apply Bellman-Ford update rule
 *    - For each entry in received DV:
 *        new_cost = neighbor_cost + advertised_cost
 *    - If this is a cheaper path, update route table
 * ------------------------------------------------------------------------- */
static bool dv_update(router_t* R, neighbor_t* nb, const dv_msg_t* m){
    bool changed = false;
    // TODO: Implement Bellman-Ford update logic
    return changed;
}

/* -------------------------------------------------------------------------
 * TODO #4: Forward data packets based on routing table
 *    - Decrement TTL
 *    - Perform LPM lookup to find next hop
 *    - Forward via UDP or deliver locally if directly connected
 * ------------------------------------------------------------------------- */
static void forward_data(router_t* R, const data_msg_t* in){
    // TODO: Implement packet forwarding using LPM
}

/* -------------------------------------------------------------------------
 * Signal handler for graceful shutdown (Ctrl+C)
 * ------------------------------------------------------------------------- */
static volatile sig_atomic_t running=1;
static void on_sigint(int _){ (void)_; running=0; }

/* -------------------------------------------------------------------------
 * Main event loop
 * ------------------------------------------------------------------------- */
int main(int argc, char** argv){
    if(argc != 2) die("Usage: %s <conf>", argv[0]);
    router_t R = {0};
    parse_conf(&R, argv[1]);

    signal(SIGINT, on_sigint);
    R.sock_ctrl = udp_bind(R.ctrl_port);
    R.sock_data = udp_bind(get_data_port(R.ctrl_port));

    time_t next_broadcast = time(NULL) + UPDATE_INTERVAL_SEC;
    log_table(&R, "init");

    //----------------------------------------------------------------------
    // Main event loop using select()
    //
    // - Wait for control (DV) or data packets
    // - Wake up periodically (every 1 second) to broadcast updates using select timeout
    // - Detect dead neighbors (no DV received for DEAD_INTERVAL_SEC)
    //----------------------------------------------------------------------
    while(running){
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(R.sock_ctrl, &rfds);
        FD_SET(R.sock_data, &rfds);
        int maxfd = (R.sock_ctrl > R.sock_data) ? R.sock_ctrl : R.sock_data;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int n = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if(n < 0 && errno == EINTR) continue;

        time_t now = time(NULL);

        // Periodic broadcast
        if(now >= next_broadcast){
            broadcast_dv(&R);
            next_broadcast = now + UPDATE_INTERVAL_SEC;
        }

        //Neighbor timeout detection
        for(int i=0; i<R.num_neighbors; i++){
            // TODO: Neighbor timeout detection (use neighbor_t's last_heard)
            // If timeout is detected, poison all routes learned from this neighbor
            // Output a log message with log_table(&R, "neighbor-dead");
        }

        //Handle control (DV) messages
        if(n > 0 && FD_ISSET(R.sock_ctrl, &rfds)){
            // TODO: Handle control (DV) messages and call dv_update
            // If the routing table is changed, output a log message with log_table(&R,"dv-update")

        }
        
        // TODO: Handle data packets
        if(n > 0 && FD_ISSET(R.sock_data, &rfds)){
            // TODO: Handle data packets and call forward_data
        }
    }

    close(R.sock_ctrl);
    close(R.sock_data);
    printf("[R%u] shutdown\n", R.self_id);
    return 0;
}