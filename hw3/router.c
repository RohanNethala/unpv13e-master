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
    msg.type = 2;
    int self_id = htons(R->self_id);
    msg.sender_id = self_id;
    uint16_t output_number;
    output_number = 0;
    int total_number_of_routes;
    total_number_of_routes = R->num_routes;
    for (int i = 0; i < total_number_of_routes; i++) {
        route_entry_t* e = &R->routes[i];
        uint32_t dest_net = e->dest_net;
        uint32_t mask = e->mask;
        uint32_t cost = e->cost;
        msg.e[output_number].net = dest_net;
        msg.e[output_number].mask = mask;
        uint16_t send_cost = cost;
        uint32_t neighbor_ip = nb->ip;
        uint32_t next_hop = e->next_hop;
        if (next_hop == neighbor_ip){
            if (cost != 0){
                send_cost = INF_COST;
            }
        }
        int network_send_cost = htons(send_cost);
        msg.e[output_number].cost = network_send_cost;
        output_number++;
    }
    msg.num = htons(output_number);
    struct in_addr ip;
    uint32_t n_ip = nb->ip;
    uint16_t neighbor_control_port = nb->ctrl_port;
    ip.s_addr = n_ip;
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(neighbor_control_port);
    dest.sin_addr.s_addr = n_ip;
    size_t hdr_sz = sizeof(msg.type) + sizeof(msg.sender_id) + sizeof(msg.num);
    size_t entry_sz = sizeof(msg.e[0]);
    size_t send_len = hdr_sz + (size_t)output_number * entry_sz;
    ssize_t n = sendto(R->sock_ctrl, &msg, send_len, 0, (struct sockaddr*)&dest, sizeof(dest));
    if (n < 0) {
        perror("sendto error");
    }
}
/* -------------------------------------------------------------------------
 * TODO #2: Broadcast DV updates to all alive neighbors
 * ------------------------------------------------------------------------- */
static void broadcast_dv(router_t* R){
    // TODO: Loop over neighbors and call send_dv() for each alive neighbor
    int total_number_of_neighbors = R->num_neighbors;
    for (int i = 0; i < total_number_of_neighbors; i++) {
        neighbor_t* nb = &R->neighbors[i];
        if (nb->alive == false) {
            continue;
        }
        struct in_addr ip;
        ip.s_addr = nb->ip;
        uint16_t neighbor_control_port = nb->ctrl_port;
        uint16_t neighbor_cost = nb->cost;
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
    uint16_t message_sender = ntohs(m->sender_id);
    uint16_t message_number = ntohs(m->num);
    if (message_number > MAX_DEST) {
        message_number = MAX_DEST;
    }
    for (uint16_t i = 0; i < message_number; i++) {
        uint32_t net = m->e[i].net;
        uint32_t mask = m->e[i].mask;
        uint16_t adv_cost = ntohs(m->e[i].cost);
        uint32_t new_cost32;
        if (adv_cost == INF_COST){
            new_cost32 = INF_COST;
        } else if (nb->cost == INF_COST) {
            new_cost32 = INF_COST;
        } else {
            new_cost32 = (uint32_t)nb->cost + (uint32_t)adv_cost;
            if (new_cost32 > INF_COST) {
                new_cost32 = INF_COST;
            }
        }
        uint16_t new_cost = (uint16_t)new_cost32;
        route_entry_t* e = rt_find_or_add(R, net, mask);
        if (!e) {
            continue;
        }


        if (e->next_hop == nb->ip) {
            if (e->cost != new_cost) {
                struct in_addr dbg_net;
                struct in_addr dbg_mask;
                struct in_addr dbg_nb;
                dbg_net.s_addr = e->dest_net;
                dbg_mask.s_addr = e->mask;
                dbg_nb.s_addr = nb->ip;
                char s_net[64];
                char s_mask[64];
                char s_nb[64];
                strncpy(s_net, inet_ntoa(dbg_net), sizeof(s_net)); s_net[sizeof(s_net)-1]=0;
                strncpy(s_mask, inet_ntoa(dbg_mask), sizeof(s_mask)); s_mask[sizeof(s_mask)-1]=0;
                strncpy(s_nb, inet_ntoa(dbg_nb), sizeof(s_nb)); s_nb[sizeof(s_nb)-1]=0;
                e->cost = new_cost;
                e->last_update = time(NULL);
                changed = true;
            }
        } else {
            if (new_cost < e->cost) {
                struct in_addr dbg_net;
                struct in_addr dbg_mask;
                struct in_addr dbg_nb;
                dbg_net.s_addr = e->dest_net;
                dbg_mask.s_addr = e->mask;
                dbg_nb.s_addr = nb->ip;
                char s_net[64];
                char s_mask[64];
                char s_nb[64];
                strncpy(s_net, inet_ntoa(dbg_net), sizeof(s_net)); s_net[sizeof(s_net)-1]=0;
                strncpy(s_mask, inet_ntoa(dbg_mask), sizeof(s_mask)); s_mask[sizeof(s_mask)-1]=0;
                strncpy(s_nb, inet_ntoa(dbg_nb), sizeof(s_nb)); s_nb[sizeof(s_nb)-1]=0;
                e->next_hop = nb->ip;
                e->cost = new_cost;
                e->last_update = time(NULL);
                changed = true;
            }
        }
    }
    (void)message_sender; // currently unused, but kept for clarity
    return changed;
}

/* -------------------------------------------------------------------------
 * TODO #4: Forward data packets based on routing table
 *    - Decrement TTL
 *    - Perform LPM lookup to find next hop
 *    - Forward via UDP or deliver locally if directly connected
 * ------------------------------------------------------------------------- */
static void forward_data(router_t* R, const data_msg_t* in){
    uint32_t destination_ip_address = in->dst_ip;
    uint8_t ttl = in->ttl;
    struct in_addr src_addr;
    struct in_addr dst_addr;
    char src_str[32];
    char dst_str[32];
    src_addr.s_addr = in->src_ip;
    dst_addr.s_addr = destination_ip_address;
    snprintf(src_str, sizeof(src_str), "%s", inet_ntoa(src_addr));
    snprintf(dst_str, sizeof(dst_str), "%s", inet_ntoa(dst_addr));
    bool is_local = false;
    int total_routes = R->num_routes;
    for (int i = 0; i < total_routes; i++) {
        route_entry_t* e = &R->routes[i];
        uint32_t next_hop = e->next_hop;
        if (next_hop == 0){
            uint32_t mask = e->mask;
            uint32_t dest_net = e->dest_net;

            if ((destination_ip_address & mask) == (dest_net & mask)) {
                is_local = true;
                break;
            }
        }
    }

    if (is_local == true) {
        return;
    }
    if (ttl == 0) {
        printf("[R%u] DROP ttl=0\n", R->self_id);
        return;
    }
    route_entry_t* route = rt_lookup(R, destination_ip_address);
    if (!route) {
        return;
    }
    if (route->cost == INF_COST) {
        struct in_addr nh;
        nh.s_addr = route->next_hop;
        return;
    }
    uint32_t next_hop_ip;
    if (route->next_hop != 0) {
        next_hop_ip = route->next_hop;
    } else {
        next_hop_ip = destination_ip_address;
    }


    bool next_hop_alive = true;
    if (route->next_hop != 0) {
        next_hop_alive = false;
        for (int i = 0; i < R->num_neighbors; i++) {
            if (R->neighbors[i].ip == route->next_hop && R->neighbors[i].alive) {
                next_hop_alive = true;
                break;
            }
        }
    }
    if (route->next_hop != 0 && !next_hop_alive) {
        struct in_addr nh;
        nh.s_addr = route->next_hop;
        return;
    }
    uint8_t new_ttl = ttl - 1;
    data_msg_t out_pkt = *in;
    out_pkt.ttl = new_ttl;

    
    uint16_t dest_port;
    uint32_t dest_udp_ip;

    if (route->next_hop != 0) {
        dest_udp_ip = route->next_hop;
        uint16_t neighbor_ctrl_port = 0;
        for (int i = 0; i < R->num_neighbors; i++) {
            if (R->neighbors[i].ip == route->next_hop) {
                neighbor_ctrl_port = R->neighbors[i].ctrl_port;
                break;
            }
        }
        dest_port = get_data_port(neighbor_ctrl_port);
    } else {
        return;
    }
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    int network_dest_port = htons(dest_port);
    dest.sin_port = network_dest_port;
    dest.sin_addr.s_addr = dest_udp_ip;
    size_t pkt_len = sizeof(data_msg_t);
    ssize_t n = sendto(R->sock_data, &out_pkt, pkt_len,0,(struct sockaddr*)&dest, sizeof(dest));

    if (n < 0) {
        perror("sendto data");
        return;
    }

    struct in_addr mask_addr;
    struct in_addr nh_addr;
    char nh_str[32], mask_str[32];
    mask_addr.s_addr = route->mask;
    nh_addr.s_addr = route->next_hop;
    snprintf(nh_str, sizeof(nh_str), "%s", inet_ntoa(nh_addr));
    snprintf(mask_str, sizeof(mask_str), "%s", inet_ntoa(mask_addr));
    printf("[R%u] FWD dst=%s via next_hop=%s mask=%s cost=%u ttl=%u\n",R->self_id,dst_str,nh_str, mask_str, route->cost, new_ttl);
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
            neighbor_t* nb = &R.neighbors[i];
            if (!nb->alive) continue;
            if (now - nb->last_heard > DEAD_INTERVAL_SEC) {
                nb->alive = false;
                bool changed = false;
                for (int j = 0; j < R.num_routes; j++) {
                    route_entry_t* re = &R.routes[j];
                    if (re->next_hop == nb->ip && re->cost != INF_COST) {
                        /* Debug: print which route is being poisoned */
                        struct in_addr dbg_net, dbg_mask, dbg_nh;
                        dbg_net.s_addr = re->dest_net;
                        dbg_mask.s_addr = re->mask;
                        dbg_nh.s_addr = nb->ip;
                        char s_net[64], s_mask[64], s_nh[64];
                        strncpy(s_net, inet_ntoa(dbg_net), sizeof(s_net)); s_net[sizeof(s_net)-1]=0;
                        strncpy(s_mask, inet_ntoa(dbg_mask), sizeof(s_mask)); s_mask[sizeof(s_mask)-1]=0;
                        strncpy(s_nh, inet_ntoa(dbg_nh), sizeof(s_nh)); s_nh[sizeof(s_nh)-1]=0;

                        re->cost = INF_COST;
                        re->last_update = now;
                        changed = true;
                    }
                }
                if (changed) log_table(&R, "neighbor-dead");
            }
        }

        //Handle control (DV) messages
        if(n > 0 && FD_ISSET(R.sock_ctrl, &rfds)){
            struct sockaddr_in from={0};
            socklen_t flen = sizeof(from);
            dv_msg_t buf;
            ssize_t r = recvfrom(R.sock_ctrl, &buf, sizeof(buf), 0,
                                 (struct sockaddr*)&from, &flen);
            if (r <= 0) {
                if (r < 0) perror("recvfrom");
            } else {
                // Validate message type
                if (buf.type != MSG_DV) {
                    // ignore unknown
                } else {
                    // Find neighbor by addr/port
                    uint32_t src_ip = from.sin_addr.s_addr; // NBO
                    uint16_t src_port = ntohs(from.sin_port);
                    // Debug: show actual source seen on socket
                    {
                        struct in_addr a; a.s_addr = src_ip;
                    }

                    neighbor_t* nb = NULL;
                    for (int k = 0; k < R.num_neighbors; k++) {
                        if (R.neighbors[k].ip == src_ip && R.neighbors[k].ctrl_port == src_port) {
                            nb = &R.neighbors[k]; break;
                        }
                    }
                    // Fallback: on loopback the kernel may set source IP to 127.0.0.1
                    // while configs use 127.0.1.x. If exact ip+port didn't match, try matching by port only.
                    if (!nb) {
                        for (int k = 0; k < R.num_neighbors; k++) {
                            if (R.neighbors[k].ctrl_port == src_port) { nb = &R.neighbors[k]; break; }
                        }
                    }

                    if (!nb) {
                        // Unknown neighbor: ignore the DV
                        // (This may happen if packet came from unexpected host/port.)
                    } else {
                        // Update liveness
                        nb->last_heard = time(NULL);
                        nb->alive = true;

                        bool changed = dv_update(&R, nb, &buf);
                        if (changed) log_table(&R, "dv-update");
                    }
                }
            }
        }
        
        // Handle data packets
        if(n > 0 && FD_ISSET(R.sock_data, &rfds)){
            struct sockaddr_in from={0};
            socklen_t flen = sizeof(from);
            data_msg_t pkt;
            ssize_t r = recvfrom(R.sock_data, &pkt, sizeof(pkt), 0,
                                 (struct sockaddr*)&from, &flen);
            if (r <= 0) {
                if (r < 0) perror("recvfrom data");
            } else {
                // Validate message type
                if (pkt.type != MSG_DATA) {
                    // ignore unknown message types
                } else {
                    forward_data(&R, &pkt);
                }
            }
        }
    }

    close(R.sock_ctrl);
    close(R.sock_data);
    printf("[R%u] shutdown\n", R.self_id);
    return 0;
}