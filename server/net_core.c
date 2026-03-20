#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <math.h>

#include "server_defs.h"
#include "alp.h"
#include "lsystem.h"
#include "utils.h"
#include "net_core.h"

// Kolejka do buforowania handover'ów
HandoverItem ho_queue[32];
int ho_head = 0;
int ho_tail = 0;

// Funkcja inicjalizująca serwer sieciowy
void init_server() {
    // Tworzenie gniazda UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Nasłuchiwanie na wszystkich interfejsach
    server_addr.sin_port = htons(SERVER_PORT); // Port 8080

    // Przypisanie adresu do gniazda
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Serwer nasluchuje na porcie %d...\n", SERVER_PORT);
}

// Obsługa rejestracji węzła
void handle_register(ALPHeader* header, char* payload) {
    uint32_t node_id = 0;
    
    // Sprawdzamy payload z ID węzła
    if (header->length >= sizeof(RegisterPayload)) {
        RegisterPayload* p = (RegisterPayload*)payload;
        node_id = p->node_id;
        
        // Opcjonalnie węzeł może zasugerować punkt startowy
        if (p->origin_x != 0 || p->origin_y != 0) {
            user_origin_x = p->origin_x;
            user_origin_y = p->origin_y;
            user_origin_set = 1;
            printf("Otrzymano User Origin od wezla %d: %d, %d\n", node_id, user_origin_x, user_origin_y);
        }
    } else {
        printf("Ostrzezenie: REGISTER bez payloadu. Ignorowanie.\n");
        return;
    }

    printf("Otrzymano REGISTER od NodeID: %u (IP: %s:%d)\n", 
           node_id, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    // Walidacja, sprawdzenie czy ten sam IP nie zgłasza się jako inny ID
    for(int i=0; i<nodes_count; i++) {
        if (nodes[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
            nodes[i].addr.sin_port == client_addr.sin_port &&
            nodes[i].id != node_id) {
            printf("!!! OSTRZEZENIE: Konflikt adresow! Node %d i Node %d maja ten sam adres %s:%d !!!\n",
                   nodes[i].id, node_id, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        }
    }

    // Jeśli węzeł już istnieje (re-register), aktualizowanie danych
    for(int i=0; i<nodes_count; i++) {
        if (nodes[i].id == node_id) {
            printf(" -> Aktualizacja adresu dla wezla ID: %d\n", node_id);
            nodes[i].addr = client_addr; 
            nodes[i].active = 1;
            
            // Wysłanie potwierdzenia
            ALPHeader ack;
            ack.type = MSG_REGISTER_ACK;
            ack.length = 0;
            sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&client_addr, addr_len);
            return;
        }
    }

    // Rejestracja nowego węzła
    if (nodes_count < MAX_NODES) {
        nodes[nodes_count].addr = client_addr;
        nodes[nodes_count].active = 1;
        nodes[nodes_count].id = node_id; 
        nodes[nodes_count].last_ack_seq = 0;
        
        printf("Zarejestrowano nowy wezel ID: %d\n", nodes[nodes_count].id);
        
        ALPHeader ack;
        ack.type = MSG_REGISTER_ACK;
        ack.length = 0;
        sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&client_addr, addr_len);
        
        nodes_count++;
    }
}

// Funkcje kolejki
void enqueue_handover(HandoverPayload* hp) {
    if ((ho_tail + 1) % 32 == ho_head) {
        printf("ERROR: Handover queue full!\n");
        return;
    }
    ho_queue[ho_tail].hp = *hp;
    ho_tail = (ho_tail + 1) % 32;
}

int dequeue_handover(HandoverPayload* hp) {
    if (ho_head == ho_tail) return 0;
    *hp = ho_queue[ho_head].hp;
    ho_head = (ho_head + 1) % 32;
    return 1;
}

// Przetwarzanie wszystkich handover'ów z kolejki
int process_pending_handovers() {
    HandoverPayload hp;
    int handled = 0;
    while(dequeue_handover(&hp)) {
        perform_handover(&hp);
        handled = 1;
    }
    return handled;
}

// Funkcja wysyłająca fragment danych w sposób Stop-and-Wait
int send_chunk_reliable(int node_idx, const char* data, int chunk_len, uint32_t seq_num) {
    char chunk_packet[sizeof(ALPHeader) + sizeof(ChunkDataHeader) + 64]; 
    ALPHeader* ch = (ALPHeader*)chunk_packet;
    ChunkDataHeader* cdh = (ChunkDataHeader*)(chunk_packet + sizeof(ALPHeader));
    char* chunk_data = (char*)(chunk_packet + sizeof(ALPHeader) + sizeof(ChunkDataHeader));
    
    ch->type = MSG_DATA_CHUNK;
    ch->length = sizeof(ChunkDataHeader) + chunk_len;
    ch->reserved = 0;
    
    cdh->seq_num = seq_num;
    memcpy(chunk_data, data, chunk_len);
    
    int packet_size = sizeof(ALPHeader) + ch->length;
    
    int retries = 5;       // Liczba prób
    int timeout_us = 500000; // Timeout 0.5 sekundy
    
    for(int attempt = 0; attempt < retries; attempt++) {
        // Wysyłka UDP
        sendto(sockfd, chunk_packet, packet_size, 0, 
               (struct sockaddr*)&nodes[node_idx].addr, addr_len);
        
        int elapsed = 0;
        int step = 1000; 
        
        // Pętla oczekiwania na ACK
        while (elapsed < timeout_us) {
            char buf[1024];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            
            // MSG_DONTWAIT pozwala nie blokować się na zawsze
            int n = recvfrom(sockfd, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr*)&from_addr, &from_len);
            
            if (n > 0) {
                if (n >= sizeof(ALPHeader)) {
                    ALPHeader* h = (ALPHeader*)buf;
                    char* payload = buf + sizeof(ALPHeader);
                    
                    if (h->type == MSG_CHUNK_ACK) {
                         // Jeśli to ACK dla fragmentu -> SUKCES
                         if (h->length >= sizeof(ChunkAckPayload)) {
                             ChunkAckPayload* ack = (ChunkAckPayload*)payload;
                             if (ack->seq_num == seq_num) {
                                 return 1; 
                             }
                         }
                    } 
                    else if (h->type == MSG_PIXEL_DATA) {
                        // Podczas czekania na ACK mogą przyjść piksele od innych węzłów
                        handle_pixel(payload, h->length);
                    }
                    else if (h->type == MSG_HANDOVER) {
                         // Jeśli przyjdzie handover podczas wysyłania chunków,
                         // trzeba przerwać wysyłanie (ABORT) i zająć się handoverem.
                         if (h->length >= sizeof(HandoverPayload)) {
                             printf("RX HANDOVER inside send_chunk_reliable. Enqueueing and ABORTING chunk.\n");
                             enqueue_handover((HandoverPayload*)payload);
                             return 2; // Kod 2 = Abort triggered by handover
                         }
                    }
                }
            } else {
                 usleep(step); 
                 elapsed += step;
            }
        }
    }
    
    printf("FAILURE: Send chunk %u to node %d (attempt %d) failed.\n", seq_num, nodes[node_idx].id, retries);
    return 0;
}

// Główna logika handover'u, gdy żółw wyjdzie poza region
void perform_handover(HandoverPayload* hp) {
    printf("HANDOVER PROCESSED: X=%d Y=%d Idx=%u\n", hp->current_x, hp->current_y, hp->char_index);
    
    int target_node_idx = -1;
    // Obliczenie współrzędnej X (dzieląc przez 1000)
    int ix = (int)floor((float)hp->current_x / 1000.0f);
    
    if (global_slice_w <= 0) global_slice_w = 1; 

    // Znalezienie, który węzeł odpowiada za ten obszar X
    if (ix < 0) target_node_idx = 0;
    else if (ix >= WIDTH) target_node_idx = nodes_count - 1;
    else {
        target_node_idx = ix / global_slice_w;
        if (target_node_idx >= nodes_count) target_node_idx = nodes_count - 1;
    }

    if (target_node_idx != -1) {
        printf(" -> Target Node: %d\n", nodes[target_node_idx].id);
        
        char packet[256]; 
        memset(packet, 0, sizeof(packet));
        ALPHeader* h = (ALPHeader*)packet;
        WorkConfig handover_cfg;
        memset(&handover_cfg, 0, sizeof(WorkConfig));

        // Konstrukcja pakietu START_WORK dla nowego węzła
        h->type = MSG_START_WORK;
        handover_cfg.start_x = hp->current_x;     // Przekazanie dokładnej pozycji
        handover_cfg.start_y = hp->current_y;
        handover_cfg.angle = hp->current_angle;
        handover_cfg.step = (int32_t)(global_step_val * 1000.0f); 
        handover_cfg.rot_angle = (int32_t)(sys.kat_obrotu * 100.0f);
        handover_cfg.draw_g = sys.draw_g ? 1 : 0;
        handover_cfg.stack_depth = hp->stack_depth; // Przekazanie stanu stosu
        
        // Kopiowanie stosu
        for(int s=0; s<8; s++) {
            handover_cfg.stack_x[s] = (s < hp->stack_depth) ? hp->stack_x[s] : 0;
            handover_cfg.stack_y[s] = (s < hp->stack_depth) ? hp->stack_y[s] : 0;
            handover_cfg.stack_angle[s] = (s < hp->stack_depth) ? hp->stack_angle[s] : 0;
        }
        
        // Obliczenie nowego regionu odpowiedzialności węzła
        int overlap = 20;
        int my_slice_start = target_node_idx * global_slice_w;
        int my_slice_end = my_slice_start + global_slice_w;
        if (target_node_idx == nodes_count - 1) my_slice_end = WIDTH;

        handover_cfg.region_min_x = my_slice_start - overlap;
        handover_cfg.region_max_x = my_slice_end + overlap;
        handover_cfg.region_min_y = -5000;
        handover_cfg.region_max_y = 5000;
        // Skrajne węzły nie mają granic zewnętrznych
        if (target_node_idx == 0) handover_cfg.region_min_x = -5000;
        if (target_node_idx == nodes_count - 1) handover_cfg.region_max_x = 5000;

        handover_cfg.start_index = hp->char_index; 
        h->length = sizeof(WorkConfig);
        memcpy(packet + sizeof(ALPHeader), &handover_cfg, sizeof(WorkConfig));

        // Wysyłka CONFIGU (dla pewności podwójnie)
        for(int k=0; k<2; k++) {
             sendto(sockfd, packet, sizeof(ALPHeader)+sizeof(WorkConfig), 0, 
                (struct sockaddr*)&nodes[target_node_idx].addr, addr_len);
             usleep(10000); // Małe opóźnienie
        }

        // Wysyłka reszty stringa L-systemu, zaczynając od char_index
        int total_len = strlen(buforA);
        int start_offset = hp->char_index;
        
        if (start_offset < total_len) {
            int chunk_size = 64;
            int offset = start_offset;
            int seq = 0; 

            while (offset < total_len) {
                int current_chunk = total_len - offset;
                if (current_chunk > chunk_size) current_chunk = chunk_size;
                
                // Wysyłka kawałka
                int result = send_chunk_reliable(target_node_idx, buforA + offset, current_chunk, seq);
                
                if (result == 0) {
                    printf("ABORT Handover stream to node %d due to FAILURE\n", nodes[target_node_idx].id);
                    break; 
                } else if (result == 2) {
                     printf("STOP Handover stream to node %d due to HANDOVER RECEIVED\n", nodes[target_node_idx].id);
                     if (process_pending_handovers()) {
                         break;
                     }
                }
                
                offset += current_chunk;
                seq++;
                
                // Sprawdzenie czy coś nie wpadło do kolejki w międzyczasie
                if (process_pending_handovers()) {
                     break;
                }
            }
            
            // Koniec danych
            ALPHeader end_msg;
            end_msg.type = MSG_END_DATA;
            end_msg.length = 0;
            end_msg.reserved = 0;
            // Wysyłka ponowna 3 razy, żeby na pewno doszło
            for(int k=0; k<3; k++) {
                sendto(sockfd, &end_msg, sizeof(end_msg), 0, 
                    (struct sockaddr*)&nodes[target_node_idx].addr, addr_len);
                usleep(5000);
            }
        }
    }
}
