#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <math.h>
#include <signal.h>

#include "server_defs.h"
#include "alp.h"
#include "lsystem.h"
#include "utils.h"
#include "net_core.h"

int main(int argc, char *argv[]) {
    // Rejestracja sygnału Ctrl+C, żeby zapisać wynik nawet jak przerwie się program
    signal(SIGINT, (void (*)(int))save_results); 

    // Inicjalizacja sieci (socket)
    init_server();
    
    // Czyszczenie siatki grid
    for(int y=0; y<HEIGHT; y++) for(int x=0; x<WIDTH; x++) grid[y][x] = ' ';

    // 1. WCZYTANIE KONFIGURACJI L-SYSTEMU
    int iterations = 0;
    char config_filename[256];
    if (argc > 1) {
        strncpy(config_filename, argv[1], sizeof(config_filename) - 1);
        config_filename[sizeof(config_filename) - 1] = '\0';
    } else {
        // Interaktywne zapytanie o plik
        printf("Podaj nazwe pliku konfiguracyjnego (np. sierpinski.txt): ");
        if (scanf("%255s", config_filename) != 1) {
             strcpy(config_filename, "sierpinski.txt");
        }
    }

    load_config(config_filename, &sys, &iterations);

    // 2. GENEROWANIE STRINGA L-SYSTEMU (w pamięci serwera)
    char buforB[MAX_STRING_LENGTH];
    strcpy(buforA, sys.aksjomat);
    
    for(int i=0; i<iterations; i++) {
        generuj_ciag(buforA, buforB, &sys);
        strcpy(buforA, buforB);
        // Zabezpieczenie przed przepełnieniem bufora
        if (strlen(buforA) > MAX_STRING_LENGTH - 1000) {
            printf("Ostrzezenie: Osiagnieto limit dlugosci ciagu przy iteracji %d\n", i+1);
            break;
        }
    }
    printf("Wygenerowano ciag: %lu znakow\n", strlen(buforA));
    printf("Oczekiwanie na wezly (Ctrl+C aby przerwac)...\n");

    char buffer[2048];
    int started = 0; // Flaga czy obliczenia już ruszyły
    
    // Ustawienie timeoutu dla recvfrom, żeby pętla nie blokowała się w nieskończoność
    // Dzięki temu można wykryć brak aktywności
    struct timeval tv;
    tv.tv_sec = 1; 
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Error setting socket timeout");
    }
    
    int silence_counter = 0;
    int frac_min_x = 0;
    int frac_max_x = WIDTH;

    // --- GŁÓWNA PĘTLA SERWERA ---
    while(1) {
        // Odbiór pakietu (nieblokujący dzięki timeoutowi, ale w praktyce blokuje na 1s)
        int n = recvfrom(sockfd, buffer, 2048, 0, (struct sockaddr*)&client_addr, &addr_len);
        
        if (n < 0) {
            // Timeout (brak danych przez 1s)
            if (started) {
                silence_counter++;
                printf("."); 
                fflush(stdout);
                // Jeśli przez 15 sekund nic się nie dzieje -> koniec pracy
                if (silence_counter >= 15) {
                    printf("\nBRAK AKTYWNOSCI PRZEZ 15 SEKUND. ZAKONCZENIE PRACY.\n");
                    save_results();
                    exit(0);
                }
            }
            continue;
        }
        
        silence_counter = 0; // Reset licznika ciszy, bo coś przyszło
        if (n < sizeof(ALPHeader)) continue; // Za krótki pakiet, śmieci

        ALPHeader* header = (ALPHeader*)buffer;
        char* payload = buffer + sizeof(ALPHeader);

        switch(header->type) {
            case MSG_REGISTER:
                handle_register(header, payload);
                
                // Jeśli jest komplet węzłów i jeszcze nie wystartowano -> START
                if (!started && nodes_count == EXPECTED_NODES) {
                    printf("Zebrano %d wezlow. Sortowanie po ID i rozpoczynanie...\n", nodes_count);
                    // Sortowanie węzłów, żeby ID 1, 2, 3 były w rosnącej kolejności
                    qsort(nodes, nodes_count, sizeof(Node), compare_nodes);

                    float base_start_x = 0.0f;
                    float base_start_y = 0.0f;
                    float base_step = 0.0f;
                    
                    // Auto-dopasowanie skali i pozycji do ekranu
                    auto_adjust(buforA, &sys, &base_start_x, &base_start_y, &base_step, &frac_min_x, &frac_max_x); 
                    
                    if (base_step <= 0.0f) base_step = 1.0f;
                    
                    // Opcjonalne nadpisanie przez węzeł obliczeniowy
                    if (user_origin_set) {
                        base_start_x = (float)user_origin_x;
                        base_start_y = (float)user_origin_y;
                        printf("UZYCIE USER ORIGIN: Start X=%.1f, Start Y=%.1f\n", base_start_x, base_start_y);
                    }
                    
                    // Podział ekranu na pionowe paski
                    if (nodes_count > 0) {
                        global_slice_w = WIDTH / nodes_count;
                    } else {
                        global_slice_w = WIDTH;
                    }
                    if (global_slice_w <= 0) global_slice_w = 1;

                    global_step_val = base_step; 

                    printf("Partycjonowanie: Ekran X=[0, %d], Pasek=%d px, Krok=%.2f\n", 
                           WIDTH, global_slice_w, global_step_val);
                    
                    printf("\n--- PRZYDZIAL STREF (Posortowane po ID) ---\n");
                    for(int i=0; i<nodes_count; i++) {
                         int s_start = i * global_slice_w;
                         int s_end = s_start + global_slice_w;
                         if (i == nodes_count - 1) s_end = WIDTH;
                         printf("Index %d -> Wezel ID %d: X [%d, %d]\n", i, nodes[i].id, s_start, s_end);
                    }
                    printf("-------------------------------------------\n\n");

                    // Wybór węzła startowego
                    int start_node_idx = 0;
                    
                    printf("Startujemy od wezla %d (Start X: %.1f, Start Y: %.1f)\n", 
                           nodes[start_node_idx].id, base_start_x, base_start_y);
                    fflush(stdout);

                    // Przygotowanie pakietu START_WORK
                    char packet[256]; 
                    memset(packet, 0, sizeof(packet));
                    ALPHeader* h = (ALPHeader*)packet;
                    WorkConfig start_cfg;
                    
                    // Konwersja float -> fixed point (int * 1000) do przesyłu sieciowego
                    start_cfg.start_x = (int32_t)roundf(base_start_x * 1000.0f);
                    start_cfg.start_y = (int32_t)roundf(base_start_y * 1000.0f);
                    start_cfg.angle = (int32_t)(sys.start_angle * 100.0f);
                    start_cfg.step = (int32_t)roundf(base_step * 1000.0f);
                    start_cfg.rot_angle = (int32_t)(sys.kat_obrotu * 100);
                    start_cfg.draw_g = sys.draw_g ? 1 : 0;
                    start_cfg.stack_depth = 0; 
                    start_cfg.start_index = 0;
                    
                    // Ustalanie granic odpowiedzialności węzła
                    int overlap = 20; // Margines błędu
                    int my_slice_start = start_node_idx * global_slice_w;
                    int my_slice_end = my_slice_start + global_slice_w;
                    if (start_node_idx == nodes_count - 1) my_slice_end = WIDTH;

                    start_cfg.region_min_x = my_slice_start - overlap;
                    start_cfg.region_max_x = my_slice_end + overlap;
                    start_cfg.region_min_y = -5000;
                    start_cfg.region_max_y = 5000;

                    // Skrajne strefy otwarte
                    if (start_node_idx == 0) start_cfg.region_min_x = -5000;
                    if (start_node_idx == nodes_count - 1) start_cfg.region_max_x = 5000;

                    h->type = MSG_START_WORK;
                    h->length = sizeof(WorkConfig);
                    memcpy(packet + sizeof(ALPHeader), &start_cfg, sizeof(WorkConfig));
                    
                    printf("Wysylanie CONFIGU do wezla %d...\n", nodes[start_node_idx].id);

                    usleep(300000); 
                    sendto(sockfd, packet, sizeof(ALPHeader)+sizeof(WorkConfig), 0, (struct sockaddr*)&nodes[start_node_idx].addr, addr_len);

                    usleep(100000); 
                    
                    // Rozpoczęcie wysyłania łańcucha znaków
                    int total_len = strlen(buforA);
                    int chunk_size = 64;
                    int offset = 0;
                    int seq_num = 0;
                    
                    while (offset < total_len) {
                        int current_chunk = total_len - offset;
                        if (current_chunk > chunk_size) current_chunk = chunk_size;
                        
                        // Wysyłanie chunka
                        int result = send_chunk_reliable(start_node_idx, buforA + offset, current_chunk, seq_num);
                        
                        if (result == 0) {
                            printf("Błąd wysyłania do węzła startowego. Przerwanie.\n");
                            break;
                        } else if (result == 2) {
                             // Otrzymano HANDOVER podczas wysyłania - przerwanie strumienia
                             // bo węzeł już przekazał pracę dalej
                             printf("Otrzymano HANDOVER od węzła startowego. Przerywanie strumienia.\n");
                             process_pending_handovers();
                             break;
                        }
                        
                        offset += current_chunk;
                        seq_num++;
                        
                        if (process_pending_handovers()) {
                             break;
                        }
                    }
                    
                    if (offset >= total_len) {
                        printf("Wyslano caly L-System (%d bajtow) sekwencjami %d.\n", total_len, seq_num);
                    }
                    
                    usleep(500000); 
                    
                    // Zakończenie transmisji danych
                    ALPHeader end_msg;
                    end_msg.type = MSG_END_DATA;
                    end_msg.length = 0;
                    end_msg.reserved = 0;
                    for(int k=0; k<3; k++) {
                        sendto(sockfd, &end_msg, sizeof(end_msg), 0, 
                            (struct sockaddr*)&nodes[start_node_idx].addr, addr_len);
                        usleep(5000);
                    }
                    
                    started = 1;
                } else if (!started) {
                    printf("Oczekiwanie na wezly (%d/%d)...\n", nodes_count, EXPECTED_NODES);
                }
                break;

            case MSG_PIXEL_DATA:
                handle_pixel(payload, header->length);
                break;
                
            case MSG_CHUNK_ACK:
                // Obsługa ACK, można zaktualizować status węzła
                if (header->length >= sizeof(uint32_t)) {
                    uint32_t ack_seq = *(uint32_t*)payload;
                    for(int i=0; i<nodes_count; i++) {
                        if (nodes[i].addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                            nodes[i].addr.sin_port == client_addr.sin_port) {
                            nodes[i].last_ack_seq = ack_seq;
                            break;
                        }
                    }
                }
                break;
                
            case MSG_WORK_DONE:
                printf("Wezel zakonczyl prace.\n");
                printf("Statystyki: Odebrano %d pikseli, w tym %d poza zakresem.\n", received_pixels, oob_pixels);
                save_results(); // Zapisanie wyniku
                break;

            case MSG_HANDOVER:
                if (header->length >= sizeof(HandoverPayload)) {
                    HandoverPayload* hp = (HandoverPayload*)payload;
                    printf("RX HANDOVER from Main Loop. Enqueueing.\n");
                    enqueue_handover(hp);
                    process_pending_handovers();
                }
                break;
        }
    }

    close(sockfd);
    return 0;
}
