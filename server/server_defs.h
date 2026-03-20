#ifndef SERVER_DEFS_H
#define SERVER_DEFS_H

#include <netinet/in.h>
#include "alp.h"

 
// KONFIGURACJA GŁÓWNA SERWERA

#define WIDTH 200            // Szerokość siatki
#define HEIGHT 200           // Wysokość siatki
#define MAX_NODES 4          // Maksymalna liczba węzłów, które serwer obsłuży
#define EXPECTED_NODES 4     // Ile węzłów serwer oczekuje przed rozpoczęciem pracy
#define MAX_STRING_LENGTH 50000 // Maksymalny rozmiar wygenerowanego ciągu L-systemu

// --- STRUKTURY PROGRAMU ---

// Pojedyncza reguła generowania fraktala (np. F -> F+G)
typedef struct {
    char przed;     // Znak, który jest zastępowany (np. 'F')
    char po[64];    // Ciąg znaków, który zastępuje
} Regula;

// Pełna definicja L-systemu (fraktala)
typedef struct {
    char aksjomat[64];      // Stan początkowy, np. "F-G-G"
    Regula reguly[5];       // Tablica możliwych reguł (max 5)
    int liczba_regul;       // Ile reguł faktycznie zdefiniowano
    float kat_obrotu;       // O jaki kąt (w stopniach) należy skręcić przy '+' i '-'
    float dlugosc_kroku;    // Bazowa długość kreski (jeszcze przed auto_adjust)
    float start_angle;      // Początkowy kąt żółwia
    int draw_g;             // Czy 'G' rysuje linię (1) czy jest tylko przesunięciem (0)
} LSystem;

// Reprezentacja podłączonego węzła (klienta) w pamięci serwera
typedef struct {
    struct sockaddr_in addr; // Adres IP i port węzła (do wysyłania UDP)
    int active;              // Flaga: 1 = aktywny, 0 = nieaktywny
    int id;                  // ID zgłoszone przez węzeł
    uint32_t last_ack_seq;   // Ostatni potwierdzony numer sekwencyjny chunk'a
} Node;

// --- ZMIENNE GLOBALNE (deklaracje extern) ---
// Fizyczne definicje tych zmiennych są w globals.c.

extern char grid[HEIGHT][WIDTH]; // Tablica pikseli reprezentująca wynikowy obraz
extern int sockfd;               // Deskryptor głównego gniazda sieciowego (socket)
extern struct sockaddr_in server_addr, client_addr; // Struktury adresowe serwera i aktualnego klienta
extern socklen_t addr_len;       // Rozmiar struktury adresu

extern Node nodes[MAX_NODES];    // Tablica przechowująca stan wszystkich węzłów
extern int nodes_count;          // Licznik aktualnie zarejestrowanych węzłów
extern int received_pixels;      // Statystyka: ile pikseli już odebrano
extern int oob_pixels;           // Statystyka: ile pikseli wypadło poza ekran

// Opcjonalne wymuszenie środka rysunku przez węzeł obliczeniowy
extern int user_origin_x;        
extern int user_origin_y;
extern int user_origin_set;

extern LSystem sys;              // Obiekt trzymający wczytany L-system
extern char buforA[MAX_STRING_LENGTH]; // Główny bufor na wygenerowany ciąg znaków (F, G, +, -...)
extern float global_step_val;    // Ostateczna długość kroku po przeskalowaniu
extern int global_slice_w;       // Szerokość pionowego paska przydzielana jednemu węzłowi

#endif
