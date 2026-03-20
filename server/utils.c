#include <stdio.h>
#include <stdlib.h>
#include "server_defs.h"

// Funkcja obsługująca odbiór piksela od węzła
void handle_pixel(char* payload, int len) {
    // Sprawdenie czy pakiet ma sensowną długość
    if (len < sizeof(PixelData)) return;
    
    // Rzutowanie surowego bufora na strukturę PixelData
    PixelData* px = (PixelData*)payload;
    
    received_pixels++;
    
    // Co 100 pikseli odświeżenie licznika na ekranie, żeby widzieć postęp
    if (received_pixels % 100 == 0) {
        printf("\rOdebrano pikseli: %d (OOB: %d)   ", received_pixels, oob_pixels);
        fflush(stdout);
    }

    // walidacja czy piksel mieści się na siatce 200x200
    if (px->x >= 0 && px->x < WIDTH && px->y >= 0 && px->y < HEIGHT) {
        grid[px->y][px->x] = px->color; // zapisanie piksela ('#')
    } else {
        oob_pixels++; // statystyka pikseli poza ekranem
    }
}

// Zapis wyników do plików
void save_results() {
    // 1. Zapis tekstowy (ASCII Art)
    FILE *f = fopen("output.txt", "w");
    if (f) {
        for(int y=0; y<HEIGHT; y++) {
            for(int x=0; x<WIDTH; x++) {
                fputc(grid[y][x], f);
            }
            fputc('\n', f);
        }
        fclose(f);
        printf("Zapisano wynik do output.txt\n");
    } else {
        perror("Blad zapisu output.txt");
    }

    // 2. Zapis w formacie PBM (Portable Bitmap)
    FILE *fp = fopen("output.pbm", "w");
    if (fp) {
        // P1 - typ pliku (tekstowy bitmap)
        // WIDTH HEIGHT - wymiary
        fprintf(fp, "P1\n%d %d\n", WIDTH, HEIGHT);
        for(int y=0; y<HEIGHT; y++) {
            for(int x=0; x<WIDTH; x++) {
                // Konwersja: '#' -> 1 (czarny), w innym przypadku -> 0 (biały)
                fprintf(fp, "%d ", (grid[y][x] == '#' ? 1 : 0));
            }
            fprintf(fp, "\n");
        }
        fclose(fp);
        printf("Zapisano obraz do output.pbm (mozna otworzyc w przegladarce obrazow)\n");
    } else {
        perror("Blad zapisu output.pbm");
    }
}
