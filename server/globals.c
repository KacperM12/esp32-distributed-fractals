#include "server_defs.h"

// --- ZMIENNE GLOBALNE ---

char grid[HEIGHT][WIDTH]; // Matryca pikseli (siatka)
int sockfd;               // Gniazdo sieciowe
struct sockaddr_in server_addr, client_addr;
socklen_t addr_len = sizeof(client_addr);

Node nodes[MAX_NODES];    // Lista zarejestrowanych węzłów
int nodes_count = 0;      // Aktualna liczba węzłów
int received_pixels = 0;  // Licznik odebranych pikseli
int oob_pixels = 0;       // Licznik pikseli poza ekranem

int user_origin_x = 0;    // Wymuszony środek (jeśli użyty przez maszynę obliczającą)
int user_origin_y = 0;
int user_origin_set = 0;

LSystem sys;              // Konfiguracja wczytanego fraktala
char buforA[MAX_STRING_LENGTH]; // Główny bufor stringa generacyjnego (choć w kodzie to tablica znaków)
float global_step_val = 1.0f;   // Końcowa, przeskalowana długość kroku
int global_slice_w = 0;         // Szerokość paska ekranu dla jednego węzła

