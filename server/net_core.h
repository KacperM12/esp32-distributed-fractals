#ifndef NET_CORE_H
#define NET_CORE_H

#include "server_defs.h"

// Inicjalizacja serwera (socket, bind)
void init_server();

// Obsługa rejestracji nowego węzła (MSG_REGISTER)
void handle_register(ALPHeader* header, char* payload);

// Struktura elementu kolejki handover'ów
typedef struct {
    HandoverPayload hp;
} HandoverItem;

// Kolejka cykliczna
extern HandoverItem ho_queue[32];
extern int ho_head;
extern int ho_tail;

// Funkcje kolejki
void enqueue_handover(HandoverPayload* hp);
int dequeue_handover(HandoverPayload* hp);
// Przetwarza wszystkie oczekujące handovery z kolejki
int process_pending_handovers();

// Wykonuje logikę handoveru: oblicza nowy węzeł i wysyła mu konfigurację + dane
void perform_handover(HandoverPayload* hp);

// Wysyła kawałek danych (chunk) i czeka na potwierdzenie (ACK)
// Zwraca 1 gdy sukces, 0 gdy błąd, 2 gdy przerwano przez inny Handover
int send_chunk_reliable(int node_idx, const char* data, int chunk_len, uint32_t seq_num);

#endif
