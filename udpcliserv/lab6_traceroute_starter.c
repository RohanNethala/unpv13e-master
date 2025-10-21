#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netdb.h>

#define DEST_PORT   33434
#define MAX_HOPS    30
#define TIMEOUT_SEC 1

static double diff_ms(const struct timeval *a, const struct timeval *b) {
    return (b->tv_sec - a->tv_sec)*1000.0 + (b->tv_usec - a->tv_usec)/1000.0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <hostname>\n", argv[0]);
        return 1;
    }

    // Resolve hostname (IPv4 only)
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_DGRAM; // UDP

    int gai = getaddrinfo(argv[1], NULL, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai));
        return 1;
    }

    struct sockaddr_in *dst = (struct sockaddr_in *)res->ai_addr;
    char dst_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dst->sin_addr, dst_str, sizeof(dst_str));
    printf("Tracing route to %s (%s)\n", argv[1], dst_str);

    // Prepare destination sockaddr
    struct sockaddr_in destaddr;
    memset(&destaddr, 0, sizeof(destaddr));
    destaddr.sin_family = AF_INET;
    destaddr.sin_addr = dst->sin_addr;
    destaddr.sin_port = htons(DEST_PORT);

    // Create UDP socket for sending
    int udp_sock = -1;
    int icmp_sock = -1;
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("socket udp");
        goto cleanup;
    }

    // Create raw ICMP socket for receiving replies
    icmp_sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (icmp_sock < 0) {
        perror("socket icmp (need sudo/root)");
        goto cleanup;
    }

    // Set receive timeout on ICMP socket
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(icmp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
    }

    // Buffer for incoming packets
    unsigned char buf[1500];
    struct sockaddr_in reply_addr;
    socklen_t reply_len = sizeof(reply_addr);

    for (int ttl = 1; ttl <= MAX_HOPS; ttl++) {
        // Set IP_TTL on UDP socket
        if (setsockopt(udp_sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
            perror("setsockopt IP_TTL");
        }

        // Send an empty UDP datagram (0 bytes) to destination port
        struct timeval send_time, recv_time;
        gettimeofday(&send_time, NULL);
        ssize_t sent = sendto(udp_sock, NULL, 0, 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
        if (sent < 0) {
            perror("sendto");
            printf("%d *\n", ttl);
            continue;
        }

        // Wait for ICMP reply (may receive unrelated ICMP; filter below)
        int done = 0;
        char addrstr[INET_ADDRSTRLEN] = "*";
        double rtt = 0.0;

        while (!done) {
            ssize_t n = recvfrom(icmp_sock, buf, sizeof(buf), 0, (struct sockaddr *)&reply_addr, &reply_len);
            if (n < 0) {
                // timeout or error
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout: print * for this hop
                    printf("%d *\n", ttl);
                    break;
                } else {
                    perror("recvfrom");
                    printf("%d *\n", ttl);
                    break;
                }
            }

            gettimeofday(&recv_time, NULL);
            rtt = diff_ms(&send_time, &recv_time);

            // Parse outer IP header
            if (n < (ssize_t)sizeof(struct ip) + 8) {
                // too small to contain ICMP header
                continue;
            }
            struct ip *ip_hdr = (struct ip *)buf;
            int ip_hdr_len = ip_hdr->ip_hl * 4;
            if (n < ip_hdr_len + (int)sizeof(struct icmp)) {
                continue;
            }
            struct icmp *icmp_hdr = (struct icmp *)(buf + ip_hdr_len);

            // For ICMP types that include the original datagram, find the original UDP header to verify dest port
            if (icmp_hdr->icmp_type == ICMP_TIME_EXCEEDED || icmp_hdr->icmp_type == ICMP_DEST_UNREACH) {
                // The ICMP payload begins after the ICMP header (8 bytes). It contains the original IP header + 8 bytes of the original payload (UDP header)
                unsigned char *payload = (unsigned char *)icmp_hdr + 8;
                if ((unsigned char *)payload + sizeof(struct ip) > buf + n) {
                    // malformed or truncated
                    continue;
                }
                struct ip *orig_ip = (struct ip *)payload;
                int orig_ip_len = orig_ip->ip_hl * 4;
                if ((unsigned char *)orig_ip + orig_ip_len + sizeof(struct udphdr) > buf + n) {
                    // truncated
                    continue;
                }
                struct udphdr *orig_udp = (struct udphdr *)((unsigned char *)orig_ip + orig_ip_len);
                unsigned short orig_dport = ntohs(orig_udp->dest);
                if (orig_dport != DEST_PORT) {
                    // not a reply to our probe, keep waiting
                    continue;
                }

                // This ICMP corresponds to our probe
                inet_ntop(AF_INET, &reply_addr.sin_addr, addrstr, sizeof(addrstr));

                if (icmp_hdr->icmp_type == ICMP_TIME_EXCEEDED) {
                    printf("%d %s %.2f ms\n", ttl, addrstr, rtt);
                    done = 1;
                } else if (icmp_hdr->icmp_type == ICMP_DEST_UNREACH) {
                    // code 3 = Port Unreachable
                    if (icmp_hdr->icmp_code == ICMP_PORT_UNREACH) {
                        printf("%d %s %.2f ms Destination reached.\n", ttl, addrstr, rtt);
                        done = 1;
                        // reached destination -> exit outer loop
                        ttl = MAX_HOPS + 1; // force exit
                    } else {
                        printf("%d %s (dest unreachable code %d) %.2f ms\n", ttl, addrstr, icmp_hdr->icmp_code, rtt);
                        done = 1;
                    }
                } else {
                    // other ICMP
                    printf("%d %s (icmp type %d code %d) %.2f ms\n", ttl, addrstr, icmp_hdr->icmp_type, icmp_hdr->icmp_code, rtt);
                    done = 1;
                }
            } else {
                // other ICMP types - ignore
                continue;
            }
        }

        // If we forced exit by setting ttl beyond MAX_HOPS, break outer loop
        if (ttl > MAX_HOPS)
            break;
    }

cleanup:
    if (udp_sock >= 0)
        close(udp_sock);
    if (icmp_sock >= 0)
        close(icmp_sock);
    if (res)
        freeaddrinfo(res);

    return 0;
}
