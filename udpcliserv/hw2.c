#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* TFTP opcodes */
#define DATA 3
#define ACK  4
#define RRQ  1
#define WRQ  2
#define ERROR 5

#define DATA_SIZE 512
#define MAX_PACKET_SIZE (4 + DATA_SIZE)

static int g_sock = -1;                       // socket fd for this transfer
static struct sockaddr_storage g_peeraddr;    // client address
static socklen_t g_peerlen;
static unsigned char g_lastpkt[MAX_PACKET_SIZE]; // last packet sent
static ssize_t g_lastpktlen = 0;
static volatile sig_atomic_t g_elapsed = 0;   // seconds since last client activity
static volatile sig_atomic_t g_abort = 0;     // set when >=10s timeout

/* forward declaration */
static void send_last_packet_from_handler();

/* resends last packet when alarm fires */
static void send_last_packet_from_handler()
{
	if (g_sock >= 0 && g_lastpktlen > 0) {
		sendto(g_sock, g_lastpkt, g_lastpktlen, 0,
			   (struct sockaddr *)&g_peeraddr, g_peerlen);
	}
}

/* count elapsed time and retransmit if needed */
static void alarm_handler(int signo)
{
	(void)signo;
	g_elapsed++;
	if (g_elapsed >= 10) { // abort after 10 seconds of silence
		g_abort = 1;
		return;
	}
	send_last_packet_from_handler();
	alarm(1); // reset alarm for next second
}

/* packet builders */
static ssize_t make_data_packet(unsigned char *buf, uint16_t blk,
                                const unsigned char *data, ssize_t datalen)
{
	buf[0] = 0; buf[1] = DATA;
	buf[2] = (blk >> 8) & 0xff;
	buf[3] = blk & 0xff;
	if (datalen > 0) memcpy(buf + 4, data, datalen);
	return 4 + datalen;
}

static ssize_t make_ack_packet(unsigned char *buf, uint16_t blk)
{
	buf[0] = 0; buf[1] = ACK;
	buf[2] = (blk >> 8) & 0xff;
	buf[3] = blk & 0xff;
	return 4;
}

static ssize_t make_error_packet(unsigned char *buf, uint16_t code, const char *msg)
{
	buf[0] = 0; buf[1] = ERROR;
	buf[2] = (code >> 8) & 0xff;
	buf[3] = code & 0xff;
	size_t mlen = msg ? strlen(msg) : 0;
	if (mlen > 0) memcpy(buf + 4, msg, mlen);
	buf[4 + mlen] = 0;
	return 5 + mlen;
}

/* parses packet for filename + mode */
static int parse_request(const unsigned char *pkt, ssize_t pktlen,
                         char *filename, size_t fnlen, char *mode, size_t mlen)
{
	if (pktlen < 4) return -1;
	const unsigned char *p = pkt + 2;
	const unsigned char *end = pkt + pktlen;

	/* extract filename */
	const unsigned char *q = memchr(p, '\0', end - p);
	if (!q) return -1;
	size_t fl = q - p;
	if (fl >= fnlen) return -1;
	memcpy(filename, p, fl); filename[fl] = '\0';
	p = q + 1;
	if (p >= end) return -1;

	/* extract mode */
	q = memchr(p, '\0', end - p);
	if (!q) return -1;
	size_t ml = q - p;
	if (ml >= mlen) return -1;
	memcpy(mode, p, ml); mode[ml] = '\0';

	return 0;
}

/* validates filename to avoid directory traversal and invalid names */
static int valid_filename(const char *fn)
{
	if (!fn || fn[0] == '\0') return 0;
	if (strchr(fn, '/') || strchr(fn, '\\')) return 0;
	if (strstr(fn, "..")) return 0;
	if (strlen(fn) > 200) return 0;
	return 1;
}

/* handles read request from client */
static void handle_rrq(int sock, struct sockaddr_storage *peeraddr,
                       socklen_t peerlen, const char *filename)
{
	FILE *fp = fopen(filename, "rb");
	if (!fp) {
		unsigned char err[516];
		ssize_t elen = make_error_packet(err, 1, "File not found");
		sendto(sock, err, elen, 0, (struct sockaddr *)peeraddr, peerlen);
		exit(1);
	}

	uint16_t block = 1;
	unsigned char data[DATA_SIZE];

	/* initializes retransmission state */
	g_sock = sock;
	memcpy(&g_peeraddr, peeraddr, peerlen);
	g_peerlen = peerlen;
	g_elapsed = 0; g_abort = 0;

	/* setup SIGALRM handler */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	/* read file blocks and send until done */
	while (1) {
		ssize_t nread = fread(data, 1, DATA_SIZE, fp);
		ssize_t pktlen = make_data_packet(g_lastpkt, block, data, nread);
		g_lastpktlen = pktlen;

		/* send data block */
		if (sendto(sock, g_lastpkt, g_lastpktlen, 0,
		           (struct sockaddr *)&g_peeraddr, g_peerlen) != g_lastpktlen) {
			fclose(fp); exit(1);
		}

		g_elapsed = 0;
		alarm(1); // wait for ack with retransmission enabled

		for (;;) {
			unsigned char rbuf[516];
			struct sockaddr_storage raddr;
			socklen_t rlen = sizeof(raddr);
			ssize_t n = recvfrom(sock, rbuf, sizeof(rbuf), 0,
			                     (struct sockaddr *)&raddr, &rlen);
			if (n < 0) {
				if (errno == EINTR) { if (g_abort) { fclose(fp); exit(1); } continue; }
				fclose(fp); exit(1);
			}

			/* ignore packets from wrong address */
			if (rlen != g_peerlen || memcmp(&raddr, &g_peeraddr, g_peerlen) != 0) continue;

			alarm(0); g_elapsed = 0;

			if (n >= 4 && rbuf[1] == ACK) {
				uint16_t ackblk = (rbuf[2] << 8) | rbuf[3];
				if (ackblk == block) { block++; break; }
				else { alarm(1); continue; }
			} else if (n >= 4 && rbuf[1] == ERROR) {
				fclose(fp); exit(1);
			} else {
				alarm(1); continue;
			}
		}

		/* finished if last block < 512 */
		if (nread < DATA_SIZE) { fclose(fp); exit(0); }
	}
}

/* handles a write request from client */
static void handle_wrq(int sock, struct sockaddr_storage *peeraddr,
                       socklen_t peerlen, const char *filename)
{
	FILE *fp = fopen(filename, "wb");
	if (!fp) {
		unsigned char err[516];
		ssize_t elen = make_error_packet(err, 2, "Access violation");
		sendto(sock, err, elen, 0, (struct sockaddr *)peeraddr, peerlen);
		exit(1);
	}

	uint16_t expected_block = 1;

	/* initializes retransmission state */
	g_sock = sock;
	memcpy(&g_peeraddr, peeraddr, peerlen);
	g_peerlen = peerlen;
	g_lastpktlen = make_ack_packet(g_lastpkt, 0); // ack for wrq
	g_elapsed = 0; g_abort = 0;

	/* setup SIGALRM handler */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	/* sends initial ack(0) */
	if (sendto(sock, g_lastpkt, g_lastpktlen, 0,
	           (struct sockaddr *)&g_peeraddr, g_peerlen) != g_lastpktlen) {
		fclose(fp); exit(1);
	}

	g_elapsed = 0; alarm(1);

	/* receive data blocks and ack */
	for (;;) {
		unsigned char rbuf[516];
		struct sockaddr_storage raddr;
		socklen_t rlen = sizeof(raddr);
		ssize_t n = recvfrom(sock, rbuf, sizeof(rbuf), 0,
		                     (struct sockaddr *)&raddr, &rlen);
		if (n < 0) {
			if (errno == EINTR) { if (g_abort) { fclose(fp); exit(1); } continue; }
			fclose(fp); exit(1);
		}

		/* ignore from wrong peer */
		if (rlen != g_peerlen || memcmp(&raddr, &g_peeraddr, g_peerlen) != 0) continue;

		alarm(0); g_elapsed = 0;

		if (n >= 4 && rbuf[1] == DATA) {
			uint16_t blk = (rbuf[2] << 8) | rbuf[3];
			if (blk == expected_block) {
				ssize_t datalen = n - 4;

				/* write data */
				if (datalen > 0 && fwrite(rbuf + 4, 1, datalen, fp) != (size_t)datalen) {
					unsigned char err[516];
					ssize_t el = make_error_packet(err, 3, "Disk full or allocation exceeded");
					sendto(sock, err, el, 0, (struct sockaddr *)&g_peeraddr, g_peerlen);
					fclose(fp); exit(1);
				}

				/* ack received block */
				g_lastpktlen = make_ack_packet(g_lastpkt, blk);
				sendto(sock, g_lastpkt, g_lastpktlen, 0,
				       (struct sockaddr *)&g_peeraddr, g_peerlen);

				expected_block++;

				/* last block < 512 --> finished */
				if (datalen < DATA_SIZE) { fclose(fp); exit(0); }

				g_elapsed = 0; alarm(1); continue;
			} else if (blk < expected_block) {
				/* duplicate block → re-ACK */
				g_lastpktlen = make_ack_packet(g_lastpkt, blk);
				sendto(sock, g_lastpkt, g_lastpktlen, 0,
				       (struct sockaddr *)&g_peeraddr, g_peerlen);
				alarm(1); continue;
			} else {
				/* out-of-order --> ignore */
				alarm(1); continue;
			}
		} else if (n >= 4 && rbuf[1] == ERROR) {
			fclose(fp); exit(1);
		} else {
			alarm(1); continue;
		}
	}
}

/* main server */
int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <start_port> <end_port>\n", argv[0]);
		exit(1);
	}

	int start = atoi(argv[1]);
	int end = atoi(argv[2]);
	if (start <= 0 || end <= 0 || end <= start) { fprintf(stderr, "Invalid port range\n"); exit(1); }

	int range = end - start + 1;
	if (range < 2) { fprintf(stderr, "Port range must contain at least 2 ports\n"); exit(1); }

	char *used = calloc(range, 1);
	if (!used) exit(1);

	/* main listening socket (first port in range) */
	int listenfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (listenfd < 0) { perror("socket"); exit(1); }
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(start);
	if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) { perror("bind"); exit(1); }
	used[0] = 1;

	/* waits for read and write requests and forks a child */
	for (;;) {
		unsigned char buf[516];
		struct sockaddr_storage cliaddr;
		socklen_t clilen = sizeof(cliaddr);
		ssize_t n = recvfrom(listenfd, buf, sizeof(buf), 0,
		                     (struct sockaddr *)&cliaddr, &clilen);
		if (n < 0) { if (errno == EINTR) continue; perror("recvfrom"); continue; }
		if (n < 2) continue;

		uint16_t opcode = (buf[0] << 8) | buf[1];
		if (opcode != RRQ && opcode != WRQ) continue;

		/* finds a free port for TID */
		int found = -1;
		for (int i = 1; i < range; ++i) { if (!used[i]) { found = i; break; } }
		if (found == -1) {
			unsigned char err[516];
			ssize_t elen = make_error_packet(err, 0, "No free ports for TID");
			sendto(listenfd, err, elen, 0, (struct sockaddr *)&cliaddr, clilen);
			continue;
		}

		int tid_port = start + found;

		pid_t pid = fork();
		if (pid < 0) { perror("fork"); continue; }
		if (pid > 0) { used[found] = 1; continue; } // parent continues

		/* child: binds to tid_port and serve request */
		int sock = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock < 0) { perror("socket"); exit(1); }
		struct sockaddr_in bindaddr;
		memset(&bindaddr, 0, sizeof(bindaddr));
		bindaddr.sin_family = AF_INET;
		bindaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		bindaddr.sin_port = htons(tid_port);
		if (bind(sock, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) < 0) {
			perror("bind child"); exit(1);
		}

		char filename[256]; char mode[32];
		if (parse_request(buf, n, filename, sizeof(filename), mode, sizeof(mode)) < 0) {
			unsigned char err[516];
			ssize_t elen = make_error_packet(err, 4, "Illegal TFTP operation");
			sendto(sock, err, elen, 0, (struct sockaddr *)&cliaddr, clilen);
			exit(1);
		}

		/* validates filename */
		if (!valid_filename(filename)) {
			unsigned char err[516];
			ssize_t elen = make_error_packet(err, 2, "Access violation: invalid filename");
			sendto(sock, err, elen, 0, (struct sockaddr *)&cliaddr, clilen);
			exit(1);
		}

		/* enforces octet/binary mode */
		for (char *p = mode; *p; ++p) *p = tolower((unsigned char)*p);
		if (strcmp(mode, "octet") != 0 && strcmp(mode, "binary") != 0) {
			unsigned char err[516];
			ssize_t elen = make_error_packet(err, 0, "Only octet mode supported");
			sendto(sock, err, elen, 0, (struct sockaddr *)&cliaddr, clilen);
			exit(1);
		}

		/* dispatches request */
		if (opcode == RRQ) {
			handle_rrq(sock, (struct sockaddr_storage *)&cliaddr, clilen, filename);
		} else {
			if (access(filename, F_OK) == 0) { /* prevent overwrite */
				unsigned char err[516];
				ssize_t elen = make_error_packet(err, 6, "File already exists");
				sendto(sock, err, elen, 0, (struct sockaddr *)&cliaddr, clilen);
				exit(1);
			}
			handle_wrq(sock, (struct sockaddr_storage *)&cliaddr, clilen, filename);
		}

		close(sock);
		exit(0);
	}

	free(used);
	close(listenfd);
	return 0;
}
