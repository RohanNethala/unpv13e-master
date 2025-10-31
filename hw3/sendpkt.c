#include "common.h"

// Usage: sendpkt <router_ctrl_port> <src_ip> <dst_ip> <ttl> <msg...>
int main(int argc,char** argv){
    if(argc<6){
        fprintf(stderr,"Usage: %s <router_ctrl_port> <src_ip> <dst_ip> <ttl> <msg...>\n", argv[0]);
        return 1;
    }
    uint16_t ctrl = (uint16_t)atoi(argv[1]);
    uint16_t data_port = get_data_port(ctrl);

    struct in_addr a;
    if(!inet_aton(argv[2], &a)){ fprintf(stderr,"bad src_ip\n"); return 2; }
    uint32_t src = a.s_addr;
    if(!inet_aton(argv[3], &a)){ fprintf(stderr,"bad dst_ip\n"); return 2; }
    uint32_t dst = a.s_addr;
    uint8_t ttl = (uint8_t)atoi(argv[4]);

    char msg[128]={0}; size_t off=0;
    for(int i=5;i<argc;i++){
        size_t L=strlen(argv[i]);
        if(off+L+1>=sizeof(msg)) break;
        memcpy(msg+off, argv[i], L); off+=L;
        if(i+1<argc) msg[off++]=' ';
    }

    int s=socket(AF_INET,SOCK_DGRAM,0); if(s<0){perror("socket"); return 3;}
    struct sockaddr_in to={0}; to.sin_family=AF_INET;
    to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    to.sin_port=htons(data_port);

    data_msg_t p={0}; p.type=MSG_DATA; p.ttl=ttl;
    p.src_ip=src; p.dst_ip=dst; p.payload_len=htons((uint16_t)off);
    memcpy(p.payload,msg,off);

    if(sendto(s,&p,sizeof(p.type)*2+sizeof(uint32_t)*2+sizeof(uint16_t)+off,0,
              (struct sockaddr*)&to,sizeof(to))<0){
        perror("sendto"); close(s); return 4;
    }
    close(s);
    return 0;
}
