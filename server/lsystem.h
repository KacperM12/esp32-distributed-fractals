#ifndef LSYSTEM_H
#define LSYSTEM_H

#include "server_defs.h"

// Funkcja wczytująca parametry L-systemu z pliku tekstowego
void load_config(const char* filename, LSystem* sys, int* iterations);

// Funkcja generująca kolejną iterację ciągu znaków L-systemu
void generuj_ciag(const char* input, char* output, LSystem* sys);

// Funkcja symulująca przebieg żółwia
// Celem jest obliczenie granic rysunku (min_x, max_x...) i dobranie skali/przesunięcia
// tak, aby cały fraktal zmieścił się na ekranie WIDTH x HEIGHT
void auto_adjust(const char* instructions, LSystem* sys, float* out_start_x, float* out_start_y, float* out_step, int* out_min_x, int* out_max_x);

// Funkcja pomocnicza do sortowania węzłów (używana przez qsort)
int compare_nodes(const void* a, const void* b);

#endif
