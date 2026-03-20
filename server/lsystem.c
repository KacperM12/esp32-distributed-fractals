#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lsystem.h"

// funkcja porownujaca wezly po id (potrzebna do sortowania qsortem)
int compare_nodes(const void* a, const void* b) {
    Node* nodeA = (Node*)a;
    Node* nodeB = (Node*)b;
    return nodeA->id - nodeB->id;
}

/*
 * funkcja auto_adjust:
 * symuluje rysowanie calego fraktala w pamieci (bez stawiania pikseli),
 * zeby znalezc jego zakres min/max x i y.
 * nastepnie oblicza skale i przesuniecie, aby rysunek idealnie wypelnil ekran.
 */
void auto_adjust(const char* instructions, LSystem* sys, float* out_start_x, float* out_start_y, float* out_step, int* out_min_x, int* out_max_x) {
    float min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    float x = 0, y = 0;
    float angle = sys->start_angle; 
    
    // prosty stos do obslugi rozgalezien '[' i ']'
    struct State { float x, y, a; } stack[1000];
    int sp = 0;

    int len = strlen(instructions);
    
    // symulacja krok po kroku
    for(int i=0; i<len; i++) {
        char cmd = instructions[i];
        if (cmd == 'F' || cmd == 'G' || cmd == 'g') {
            // ruch do przodu: obliczenie nowej pozycji z trygonometrii
            float rad = angle * (M_PI / 180.0f);
            x += cos(rad) * sys->dlugosc_kroku;
            y += sin(rad) * sys->dlugosc_kroku;
            
            // aktualizacja granic rysunku
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
        else if (cmd == '+') angle += sys->kat_obrotu;
        else if (cmd == '-') angle -= sys->kat_obrotu;
        else if (cmd == '[') {
             // zapisanie stanu na stosie
             if (sp < 100) { stack[sp].x = x; stack[sp].y = y; stack[sp].a = angle; sp++; }
        }
        else if (cmd == ']') {
             // przywrócenie stanu ze stosu
             if (sp > 0) { sp--; x = stack[sp].x; y = stack[sp].y; angle = stack[sp].a; }
        }
    }

    // obliczenie wymiarow fraktala w jednostkach logicznych
    float frac_w = max_x - min_x;
    float frac_h = max_y - min_y;
    // zabezpieczenie przed dzieleniem przez zero
    if (frac_w == 0) frac_w = 1;
    if (frac_h == 0) frac_h = 1;

    // margines 5%
    float margin = 0.05f;
    float avail_w = WIDTH * (1.0f - 2*margin);
    float avail_h = HEIGHT * (1.0f - 2*margin);

    // obliczenie skali: ile pikseli przypada na jednostkê logiczn¹
    float scale_x = avail_w / frac_w;
    float scale_y = avail_h / frac_h;
    
    // wybór mniejszej skali, ¿eby zmieœciæ siê w pionie i poziomie (zachowanie proporcji)
    float scale = (scale_x < scale_y) ? scale_x : scale_y; 

    // jesli w pliku podano konkretny STEP (>0), to nale¿y go u¿yæ i tylko wycentrowaæ
    // jesli step=0 (lub mniej), to nale¿y u¿yæ obliczonej skali (auto-fit)
    if (sys->dlugosc_kroku > 0) {
        *out_step = sys->dlugosc_kroku; // z pliku
        scale = 1.0f; 
        
        // centrowanie bez skalowania
        float center_frac_x_raw = (min_x + max_x) / 2.0f;
        float center_frac_y_raw = (min_y + max_y) / 2.0f;
        
        float center_screen_x = WIDTH / 2.0f;
        float center_screen_y = HEIGHT / 2.0f;
        
        *out_start_x = center_screen_x - center_frac_x_raw;
        *out_start_y = center_screen_y - center_frac_y_raw;
        
        scale = 1.0f; 
        
    } else {
        // skalowanie kroku
        *out_step = sys->dlugosc_kroku * scale;
        
        // obliczenie œrodka fraktala
        float center_frac_x = (min_x + max_x) / 2.0f * scale;
        float center_frac_y = (min_y + max_y) / 2.0f * scale;

        float center_screen_x = WIDTH / 2.0f;
        float center_screen_y = HEIGHT / 2.0f;

        // ustawienie ¿eby by³o na œrodku
        *out_start_x = center_screen_x - center_frac_x;
        *out_start_y = center_screen_y - center_frac_y;
    }

    // zwracanie granic dla logów
    if (out_min_x) *out_min_x = (int)floor(*out_start_x + min_x * scale);
    if (out_max_x) *out_max_x = (int)ceil(*out_start_x + max_x * scale);

    printf("Auto-adjust: Skala=%.2f, Start=(%.1f, %.1f), Zakres X=[%d, %d]\n", 
           scale, *out_start_x, *out_start_y, 
           out_min_x ? *out_min_x : 0, out_max_x ? *out_max_x : 0);
}

// funkcja parsuj¹ca CONFIG
void load_config(const char* filename, LSystem* sys, int* iterations) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("blad otwierania pliku");
        exit(EXIT_FAILURE);
    }

    char line[128];
    // domyœlne wartoœci
    sys->liczba_regul = 0;
    sys->start_angle = 0.0f; 
    sys->draw_g = 1; 
    *iterations = 3;

    while (fgets(line, sizeof(line), f)) {
        // usuniêcie enter'ów
        line[strcspn(line, "\r\n")] = 0;

        // rozpoznawanie kluczy
        if (strncmp(line, "ITERATIONS:", 11) == 0) {
            sscanf(line, "ITERATIONS: %d", iterations);
        } else if (strncmp(line, "START_ANGLE:", 12) == 0) {
            sscanf(line, "START_ANGLE: %f", &sys->start_angle);
        } else if (strncmp(line, "AXIOM:", 6) == 0) {
            sscanf(line, "AXIOM: %s", sys->aksjomat);
        } else if (strncmp(line, "ANGLE:", 6) == 0) {
            sscanf(line, "ANGLE: %f", &sys->kat_obrotu);
        } else if (strncmp(line, "STEP:", 5) == 0) {
            sscanf(line, "STEP: %f", &sys->dlugosc_kroku);
        } else if (strncmp(line, "DRAW_G:", 7) == 0) {
            int val = 1;
            if (sscanf(line, "DRAW_G: %d", &val) == 1) {
                sys->draw_g = val ? 1 : 0;
            }
        } else if (strncmp(line, "RULE:", 5) == 0) {
            char p;
            char r[64];
            // formatowanie regu³y: f=f+g
            if (sscanf(line, "RULE: %c=%s", &p, r) == 2) {
                if (sys->liczba_regul < 5) {
                    sys->reguly[sys->liczba_regul].przed = p;
                    strcpy(sys->reguly[sys->liczba_regul].po, r);
                    sys->liczba_regul++;
                }
            }
        }
    }
    fclose(f);
    printf("Wczytano konfiguracje z %s:\n", filename);
    printf(" -> Iteracje: %d\n", *iterations);
    printf(" -> Start Angle: %.2f\n", sys->start_angle);
    printf(" -> Angle: %.2f\n", sys->kat_obrotu);
    printf(" -> Step: %.2f\n", sys->dlugosc_kroku);
    printf(" -> Draw G: %s\n", sys->draw_g ? "tak" : "nie");
    printf(" -> Reguly: %d\n", sys->liczba_regul);
}

// funkcja generuj¹ca ci¹g iteracyjnie
void generuj_ciag(const char* input, char* output, LSystem* sys) {
    output[0] = '\0';
    // iteracja po znakach
    for (int i = 0; input[i] != '\0'; i++) {
        char c = input[i];
        int znaleziono = 0;
        // sprawdzenie czy jest regula
        for (int r = 0; r < sys->liczba_regul; r++) {
            if (sys->reguly[r].przed == c) {
                // jeœli tak to dodanie znaków
                strcat(output, sys->reguly[r].po);
                znaleziono = 1;
                break;
            }
        }
        // jeœli nie ma reguly to przepisanie znaków
        if (!znaleziono) {
            int len = strlen(output);
            output[len] = c;
            output[len+1] = '\0';
        }
    }
}
