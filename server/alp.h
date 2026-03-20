#ifndef ALP_H
#define ALP_H

#include <stdint.h>

/* 
 * DEFINICJE TYPÓW WIADOMOŚCI PROTOKOŁU ALP (Application Layer Protocol)
 * Każdy typ to jeden bajt w nagłówku. Służą do sterowania logiką klient-serwer.
 */
#define MSG_REGISTER     0x01 // Rejestracja węzła (wysyła węzeł)
#define MSG_REGISTER_ACK 0x02 // Potwierdzenie rejestracji (wysyła serwer)
#define MSG_START_WORK   0x03 // Rozkaz rozpoczęcia pracy + konfiguracja (serwer -> węzeł)
#define MSG_PIXEL_DATA   0x04 // Wynik pracy: pojedynczy piksel (węzeł -> serwer)
#define MSG_WORK_DONE    0x05 // Zgłoszenie zakończenia pracy w swoim obszarze (węzeł -> serwer)
#define MSG_HANDOVER     0x06 // Przekazanie stanu żółwia do następnego węzła
#define MSG_DATA_CHUNK   0x07 // Fragment danych znaków L-systemu (serwer -> węzeł)
#define MSG_CHUNK_ACK    0x08 // Potwierdzenie odebrania fragmentu
#define MSG_END_DATA     0x09 // Oznacznik końca przesyłania znaków

// Stałe konfiguracyjne sieci
#define MAX_PAYLOAD 1024      // Maksymalny rozmiar użytecznych danych w pakiecie
#define SERVER_PORT 8080      // Port, na którym nasłuchuje serwer
#define NODE_PORT   9090      // Port, na którym nasłuchują węzły obliczeniowe

/* 
 * STRUKTURY DANYCH PROTOKOŁU
 * Użycie __attribute__((packed)) jest po to, żeby kompilator nie dodawał pustych bajtów (padding).
 * Dzięki temu struktury mają dokładnie taki rozmiar w bajtach, jak zdefiniowano,
 * co jest kluczowe przy przesyłaniu przez sieć.
 */

// Główny nagłówek każdego pakietu ALP
typedef struct __attribute__((packed)) {
    uint8_t type;       // Typ wiadomości (np. MSG_REGISTER)
    uint16_t length;    // Długość danych (payloadu), które idą po nagłówku
    uint8_t reserved;   // Zarezerwowane (wyrównanie do 4 bajtów)
} ALPHeader;

// Konfiguracja pracy wysyłana do węzła (MSG_START_WORK)
typedef struct __attribute__((packed)) {
    int32_t start_x;        // Współrzędna startowa X żółwia dla tego węzła
    int32_t start_y;        // Współrzędna startowa Y żółwia
    int32_t angle;          // Kąt początkowy żółwia
    int32_t step;           // Długość kroku (skalowana do int, np. float * 1000)
    int32_t rot_angle;      // Kąt obrotu L-systemu
    int16_t region_min_x;   // Początek regionu (paska), za który odpowiada węzeł
    int16_t region_max_x;   // Koniec regionu odpowiedzialności
    int16_t region_min_y;   // (Opcjonalnie, z powodzeniem użyto zakresu poziomego) zakres pionowy
    int16_t region_max_y;   
    uint32_t start_index;   // Indeks w tablicy znaków L-systemu, od którego jest start
    uint8_t draw_g;         // Flaga, czy litera 'G' rysuje znak (1) czy nie (0)
    uint8_t stack_depth;    // Głębokość stosu (jeśli start jest w środku rekurencji)
    uint16_t reserved2;     // Wyrównanie
    // Pobranie stanu stosu (jeśli handover nastąpił w trakcie)
    int32_t stack_x[8];
    int32_t stack_y[8];
    int32_t stack_angle[8];
} WorkConfig;

// Struktura pojedynczego piksela (MSG_PIXEL_DATA)
typedef struct __attribute__((packed)) {
    int16_t x;      // X na ekranie
    int16_t y;      // Y na ekranie
    uint8_t color;  // Wyświetlany znak, czyli "#"
} PixelData;

// Dane rejestracyjne węzła (MSG_REGISTER)
typedef struct __attribute__((packed)) {
    uint32_t node_id; // Unikalne ID węzła
    int16_t origin_x; // Preferowany punkt środka (opcjonalnie)
    int16_t origin_y; 
} RegisterPayload;

// Przekazanie stanu do następnego węzła (MSG_HANDOVER)
typedef struct __attribute__((packed)) {
    int32_t current_x;     // Gdzie żółw skończył w regionie aktualnego węzła
    int32_t current_y;
    int32_t current_angle;
    uint32_t char_index;   // Na którym znaku z tablicy skończył
    uint8_t stack_depth;   // Aktualny stan stosu nawiasów '[' ']'
    uint8_t reserved_pad[3]; 
    // Zawartość stosu (do 8 poziomów)
    int32_t stack_x[8];
    int32_t stack_y[8];
    int32_t stack_angle[8];
} HandoverPayload;

// Potwierdzenie odbioru fragmentu (MSG_CHUNK_ACK)
typedef struct __attribute__((packed)) {
    uint32_t seq_num; // Numer sekwencyjny potwierdzanego fragmentu
} ChunkAckPayload;

// Nagłówek fragmentu danych (MSG_DATA_CHUNK)
typedef struct __attribute__((packed)) {
    uint32_t seq_num; // Numer sekwencyjny
} ChunkDataHeader;

#endif
