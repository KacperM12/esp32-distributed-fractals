#ifndef UTILS_H
#define UTILS_H

// Przetwarzanie pakietu z pojedynczym pikselem odebranym od klienta
// Sprawdzenie czy piksel mieści się w ekranie i zapisanie go na siatce grid
void handle_pixel(char* payload, int len);

// Zapisanie wynik pracy do plików (output.txt i output.pbm)
void save_results();

#endif
