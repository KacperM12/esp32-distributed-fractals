#include "ZsutEthernet.h"
#include "ZsutEthernetUdp.h"
#include "ZsutFeatures.h"

// --- KONFIGURACJA ---
#define MANUAL_INSTANCE_ID 0 

// Definicje protokołu
#define MSG_REGISTER     0x01
#define MSG_REGISTER_ACK 0x02
#define MSG_START_WORK   0x03
#define MSG_PIXEL_DATA   0x04
#define MSG_WORK_DONE    0x05
#define MSG_HANDOVER     0x06
#define MSG_DATA_CHUNK   0x07
#define MSG_CHUNK_ACK    0x08
#define MSG_END_DATA     0x09

#define SERVER_PORT 8080
#define NODE_PORT   9090 

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint16_t length;
    uint8_t reserved;
} ALPHeader;

typedef struct __attribute__((packed)) {
    int32_t start_x;
    int32_t start_y;
    int32_t angle;      
    int32_t step;       
    int32_t rot_angle;  
    int16_t region_min_x;
    int16_t region_max_x;
    int16_t region_min_y;
    int16_t region_max_y;
    uint32_t start_index;
    uint8_t draw_g;     // Czy G rysuje linię (0=nie, 1=tak)
    uint8_t stack_depth; // Głębokość stosu
    uint16_t reserved2; // Wyrównanie
    // Stos nawrotów (8 poziomów)
    int32_t stack_x[8];
    int32_t stack_y[8];
    int32_t stack_angle[8];
} WorkConfig;

typedef struct __attribute__((packed)) {
    int16_t x;
    int16_t y;
    uint8_t color;
} PixelData;

typedef struct __attribute__((packed)) {
    uint32_t node_id;
    int16_t origin_x;
    int16_t origin_y;
} RegisterPayload;

typedef struct __attribute__((packed)) {
    int32_t current_x;
    int32_t current_y;
    int32_t current_angle; 
    uint32_t char_index;
    uint8_t stack_depth; // Głębokość stosu (0-8)
    uint8_t reserved_pad[3]; // Wyrównanie
    // Stos nawrotów (8 poziomów)
    int32_t stack_x[8];
    int32_t stack_y[8];
    int32_t stack_angle[8];
} HandoverPayload;

typedef struct __attribute__((packed)) {
    uint32_t seq_num;
} ChunkAckPayload;

typedef struct __attribute__((packed)) {
    uint32_t seq_num;
} ChunkDataHeader;

// --- ZMIENNE SIECIOWE ---
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // Unikalny MAC
unsigned int localPort = NODE_PORT;
ZsutIPAddress serverIP(192, 168, 56, 103); // Adres IP serwera
uint32_t myNodeID = 0; // Unikalne ID węzła
int16_t myOriginX = 0;
int16_t myOriginY = 0;

ZsutEthernetUDP Udp;
char packetBuffer[350];

// --- STAN ---
bool registered = false;
bool working = false;
bool receiving_data = false; // Flaga, czy trwa odbieranie chunków
int instance_id = -1; 

// Bufor stosu dla L-Systemu
struct State { float x, y, a; } l_stack[8]; 

// Wartość bezwzględna float
float my_abs(float v) {
  return (v < 0) ? -v : v;
}

// Modulo float (zamiast fmod z math.h)
float my_fmod(float a, float b) {
    while (a >= b) a -= b;
    while (a < 0) a += b;
    return a;
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  Serial.println("Start wezla IoT...");
  
  // Konfiguracja: Odczyt ID węzła z wejścia analogowego Z0 (A0)
  // W pliku infile.txt definiuje się to jako:
  // + nodeId,quantity,Z0
  // : 0,nodeId,X  (gdzie X to numer węzła: 0, 1, 2, 3)
  
  instance_id = ZsutAnalog0Read();
  
  Serial.print("Odczytano Instance ID z wejscia Z0: ");
  Serial.println(instance_id);
  
  // Walidacja, gdyby odczyt był większy niż oczekiwany
  if (instance_id > 10) {
      instance_id = 0; // Domyślnie 0
      Serial.println("Wartosc spoza zakresu 0-4, ustawiam domyslnie 0");
  }

  // Ustawienie ziarna losowości na podstawie ID instancji
  unsigned long seed = 12345 + (instance_id * 777);
  randomSeed(seed);

  // Unikalny MAC (ostatni bajt)
  mac[5] = (byte)(10 + instance_id);

  // Inicjalizacja Ethernet
  ZsutEthernet.begin(mac); 
  
  Serial.print("IP wezla: ");
  Serial.println(ZsutEthernet.localIP());

  // Sztywno wyliczony port na podstawie ID
  // Node 0 -> 10000
  // Node 1 -> 10100
  // Node 2 -> 10200
  // Node 3 -> 10300
  localPort = 10000 + (instance_id * 100);
  
  // Próba otwarcia portu
  if (Udp.begin(localPort)) {
      Serial.print("SUKCES: Nasluchuje na porcie UDP: ");
      Serial.println(localPort);
  } else {
      Serial.print("BLAD: Nie udalo sie otworzyc portu ");
      Serial.println(localPort);
  }

  // Generowanie ID węzła
  myNodeID = seed + localPort;
  Serial.print("Moje ID wezla: ");
  Serial.println(myNodeID);
}

// --- WYSYŁANIE PIKSELA ---
void sendPixel(int x, int y, char c) {
    char buffer[sizeof(ALPHeader) + sizeof(PixelData)];
    ALPHeader* h = (ALPHeader*)buffer;
    PixelData* p = (PixelData*)(buffer + sizeof(ALPHeader));

    h->type = MSG_PIXEL_DATA;
    h->length = sizeof(PixelData);
    h->reserved = 0;
    
    p->x = (int16_t)x;
    p->y = (int16_t)y;
    p->color = c;

    Udp.beginPacket(serverIP, SERVER_PORT);
    Udp.write(buffer, sizeof(buffer));
    Udp.endPacket();
    
}

// --- LOGIKA RENDEROWANIA ---
// Zmienne stanu globalnego dla węzła renderującego
float curr_x, curr_y, curr_angle, curr_step, curr_rot_angle;
uint32_t global_char_index = 0;
int16_t reg_min_x, reg_max_x, reg_min_y, reg_max_y;
uint8_t curr_draw_g = 1;
int sp = 0; // Stos globalny dla nawrotów []
uint32_t chunk_seq = 0; // ACK dla chunk'ów

void initRenderState(WorkConfig* cfg) {
    curr_x = (float)cfg->start_x / 1000.0f;
    curr_y = (float)cfg->start_y / 1000.0f;
    curr_angle = (float)cfg->angle / 100.0f;
    curr_step = (float)cfg->step / 1000.0f;
    curr_rot_angle = (float)cfg->rot_angle / 100.0f;
    
    reg_min_x = cfg->region_min_x;
    reg_max_x = cfg->region_max_x;
    reg_min_y = cfg->region_min_y;
    reg_max_y = cfg->region_max_y;
    curr_draw_g = cfg->draw_g;
    
    global_char_index = cfg->start_index;
    
    // Reset stosu tylko jeśli to nie handover
    if (cfg->start_index == 0) {
        sp = 0;
        Serial.println("INIT: Nowa praca, sp=0");
    } else {
        // Handover - odtwórz stos
        sp = (cfg->stack_depth > 8) ? 8 : cfg->stack_depth;
        for(int s=0; s<sp; s++) {
            l_stack[s].x = (float)cfg->stack_x[s] / 1000.0f;
            l_stack[s].y = (float)cfg->stack_y[s] / 1000.0f;
            l_stack[s].a = (float)cfg->stack_angle[s] / 100.0f;
        }
        Serial.print("INIT: Handover, odtworzono sp=");
        Serial.println(sp);
    }
    

}

void renderChunk(char* instructions) {
    int len = strlen(instructions);
    
    for(int i=0; i<len; i++) {
        char cmd = instructions[i];
        
        if (cmd == 'F' || cmd == 'G') {
            float rad = curr_angle * (3.14159f / 180.0f);
            float newX = curr_x + cos(rad) * curr_step;
            float newY = curr_y + sin(rad) * curr_step;
            
            // F zawsze rysuje, G rysuje tylko gdy draw_g == 1
            bool shouldDraw = (cmd == 'F') || (cmd == 'G' && curr_draw_g == 1);
            
            if (shouldDraw) {
                // Rysowanie linii
                float dx = newX - curr_x;
                float dy = newY - curr_y;
                float steps = (my_abs(dx) > my_abs(dy)) ? my_abs(dx) : my_abs(dy);
                if (steps < 1) steps = 1;
                
                float xi = dx / steps;
                float yi = dy / steps;
                float cx = curr_x;
                float cy = curr_y;
                
                for(int s=0; s<=(int)steps; s++) {
                    int ix = (int)cx;
                    int iy = (int)cy;
                    
                    // Rysowanie wszystkich pikseli w ramach bieżącego ruchu
                    sendPixel(ix, iy, '#');
                    
                    cx += xi;
                    cy += yi;
                }
            }
            
            // Aktualizowanie pozycji niezależnie od tego czy rysowano
            curr_x = newX;
            curr_y = newY;
        }
        else if (cmd == '+') {
             curr_angle += curr_rot_angle;
             // Normalizacja kąta 0-360
             while (curr_angle >= 360.0f) curr_angle -= 360.0f;
             while (curr_angle < 0.0f) curr_angle += 360.0f;
        }
        else if (cmd == '-') {
             curr_angle -= curr_rot_angle;
             // Normalizacja kąta 0-360
             while (curr_angle >= 360.0f) curr_angle -= 360.0f;
             while (curr_angle < 0.0f) curr_angle += 360.0f;
        }
        else if (cmd == '[') {
            if (sp < 8) { l_stack[sp].x = curr_x; l_stack[sp].y = curr_y; l_stack[sp].a = curr_angle; sp++; }
        }
        else if (cmd == ']') {
            if (sp > 0) { sp--; curr_x = l_stack[sp].x; curr_y = l_stack[sp].y; curr_angle = l_stack[sp].a; }
        }

        // Sprawdzenie handover'u
        if (curr_x < reg_min_x || curr_x >= reg_max_x) {
             char h_buf[sizeof(ALPHeader) + sizeof(HandoverPayload)];
             ALPHeader* hh = (ALPHeader*)h_buf;
             HandoverPayload* hp = (HandoverPayload*)(h_buf + sizeof(ALPHeader));
             
             hh->type = MSG_HANDOVER;
             hh->length = sizeof(HandoverPayload);
             hh->reserved = 0;
             
             hp->current_x = (int32_t)(curr_x * 1000);
             hp->current_y = (int32_t)(curr_y * 1000);
             hp->current_angle = (int32_t)(curr_angle * 100);
             // Przekazanie indeksu następnego znaku (i + 1) ponieważ bieżący znak (i) został właśnie wykonany
             hp->char_index = global_char_index + i + 1;
             
             // Przekazywanie w stosie nawrotów
             hp->stack_depth = (sp > 8) ? 8 : sp;
             for(int s=0; s<8; s++) {
                 if (s < hp->stack_depth) {
                     hp->stack_x[s] = (int32_t)(l_stack[s].x * 1000);
                     hp->stack_y[s] = (int32_t)(l_stack[s].y * 1000);
                     hp->stack_angle[s] = (int32_t)(l_stack[s].a * 100);
                 } else {
                     hp->stack_x[s] = 0;
                     hp->stack_y[s] = 0;
                     hp->stack_angle[s] = 0;
                 }
             }
             
             Serial.print("HANDOVER at char ");
             Serial.print(hp->char_index);
             Serial.print(" stack_depth=");
             Serial.println(hp->stack_depth);
             
             Udp.beginPacket(serverIP, SERVER_PORT);
             Udp.write(h_buf, sizeof(h_buf));
             Udp.endPacket();
             
             working = false;
             receiving_data = false; // Zatrzymanie odbierania
             return; 
        }
    }

    global_char_index += len;
}

// --- LOOP ---
void loop() {
    // 1. Rejestracja (jeśli węzeł nie jest zarejestrowany)
    if (!registered) {
        static unsigned long lastReg = 0;
        if (ZsutMillis() - lastReg > 2000) {
            
            // Próba odczytu konfiguracji (tylko raz, przy pierwszej rejestracji)
            static bool configRead = false;
            if (!configRead && instance_id == 0) { 
                 
                // Odczyt współrzędnych startowych (tylko dla węzła 0)
                // W pliku infile.txt:
                // + originX,quantity,Z4
                // + originY,quantity,Z5

                 Serial.println("DEBUG: Proba odczytu Z4/Z5 w loop...");
                 int val1 = ZsutAnalog4Read();
                 int val2 = ZsutAnalog5Read();
                 
                 // Jeśli odczytano wartości niższe niż domyślne maksymalne, następuje zapis
                 if (val1 < 1024) myOriginX = (int16_t)val1;
                 if (val2 < 1024) myOriginY = (int16_t)val2;
                 
                 Serial.print("DEBUG: Odczytano Origin: ");
                 Serial.print(myOriginX);
                 Serial.print(", ");
                 Serial.println(myOriginY);
                 configRead = true;
            }

            Serial.println("Wysylam REGISTER...");
            
            char buffer[sizeof(ALPHeader) + sizeof(RegisterPayload)];
            ALPHeader* h = (ALPHeader*)buffer;
            RegisterPayload* p = (RegisterPayload*)(buffer + sizeof(ALPHeader));

            h->type = MSG_REGISTER;
            h->length = sizeof(RegisterPayload);
            h->reserved = 0;
            p->node_id = myNodeID;
            p->origin_x = myOriginX;
            p->origin_y = myOriginY;
            
            Udp.beginPacket(serverIP, SERVER_PORT);
            Udp.write(buffer, sizeof(buffer));
            Udp.endPacket();
            
            lastReg = ZsutMillis();
        }
    }

    // 2. Odbiór pakietów - przetwarzanie wszystkich dostępnych pakietów
    while (Udp.parsePacket()) {
        int packetSize = Udp.available();
        if (packetSize == 0) continue;
        
        // Zabezpieczenie przed przepełnieniem bufora (walidacja)
        if (packetSize > 349) packetSize = 349; 
        
        Udp.read(packetBuffer, packetSize);
        packetBuffer[packetSize] = '\0'; // znak pusty, żeby było wiadomo, gdzie jest koniec
        
        ALPHeader* h = (ALPHeader*)packetBuffer;
        
        if (h->type == MSG_REGISTER_ACK) {
            Serial.println("Otrzymano ACK! Jestem zarejestrowany.");
            registered = true;
        }
        else if (h->type == MSG_DATA_CHUNK) {
             Serial.print("RX PKT: DATA CHUNK sz=");
             Serial.println(packetSize);

             if (working) {
                // Odczyt sekwencji (lecz na początku walidacja)
                if (packetSize < sizeof(ALPHeader) + sizeof(uint32_t)) {
                    Serial.println("BLAD: Za krotki chunk!");
                    continue; // Za krótki pakiet
                }

                ChunkDataHeader* cdh = (ChunkDataHeader*)(packetBuffer + sizeof(ALPHeader));
                uint32_t seq = cdh->seq_num;
                
                Serial.print("CHUNK SEQ: ");
                Serial.print(seq);
                Serial.print(" EXPECTED: ");
                Serial.println(chunk_seq);

                // Wysłanie ACK 
                char ackBuf[sizeof(ALPHeader) + sizeof(ChunkAckPayload)];
                ALPHeader* ackH = (ALPHeader*)ackBuf;
                ChunkAckPayload* ackP = (ChunkAckPayload*)(ackBuf + sizeof(ALPHeader));
                
                ackH->type = MSG_CHUNK_ACK;
                ackH->length = sizeof(ChunkAckPayload);
                ackH->reserved = 0;
                ackP->seq_num = seq;
                
                Udp.beginPacket(serverIP, SERVER_PORT);
                Udp.write(ackBuf, sizeof(ackBuf));
                Udp.endPacket();
                
                // Sprawdzenie, czy to nowy pakiet czy duplikat
                if (seq == chunk_seq) {
                    char* data = (char*)(packetBuffer + sizeof(ALPHeader) + sizeof(uint32_t));
                    int len = packetSize - (sizeof(ALPHeader) + sizeof(uint32_t));
                    
                    if (len > 0) {
                        data[len] = '\0'; // Zakończenie ciągu znaków
                        Serial.print("Rendering len=");
                        Serial.println(len);
                        renderChunk(data);
                    }
                    chunk_seq++; // Oczekiwanie na kolejny
                } else {
                    Serial.println("Duplikat lub zla sekwencja - pomijam render");
                }
             }
        }
        else if (h->type == MSG_START_WORK) {
             WorkConfig cfg;
             // Sprawdzenie rozmiaru
             int expectedSize = sizeof(ALPHeader) + sizeof(WorkConfig);
             
             if (packetSize < expectedSize) {
                 Serial.print("BLAD: Za maly pakiet START_WORK! Otrzymano: ");
                 Serial.println(packetSize);
             } else {
                 // Bezpieczna kopia danych do struktury lokalnej
                 memcpy(&cfg, packetBuffer + sizeof(ALPHeader), sizeof(WorkConfig));
                 
                 Serial.print("Otrzymano CONFIG: StartXY=( ");
                 Serial.print(cfg.start_x); Serial.print(", "); Serial.print(cfg.start_y);
                 Serial.print(") Angle="); Serial.println(cfg.angle);
                 
                 working = true;
                 chunk_seq = 0; // Reset licznika ACK
                 receiving_data = true; // Rozpoczęcie odbierania danych

                 initRenderState(&cfg);
             }
        }
        else if (h->type == MSG_END_DATA) {
             if (working) {
                 Serial.println("END_DATA: Koniec transmisji");
                 receiving_data = false; // Koniec odbierania
                 // Sprawdzenie czy jest potrzebny handover
                 if (curr_x < reg_min_x || curr_x >= reg_max_x) {
                     char h_buf[sizeof(ALPHeader) + sizeof(HandoverPayload)];
                     ALPHeader* hh = (ALPHeader*)h_buf;
                     HandoverPayload* hp = (HandoverPayload*)(h_buf + sizeof(ALPHeader));
                     
                     hh->type = MSG_HANDOVER;
                     hh->length = sizeof(HandoverPayload);
                     hh->reserved = 0;
                     
                     hp->current_x = (int32_t)(curr_x * 1000);
                     hp->current_y = (int32_t)(curr_y * 1000);
                     hp->current_angle = (int32_t)(curr_angle * 100);
                     hp->char_index = global_char_index;
                     hp->stack_depth = (sp > 8) ? 8 : sp;
                     
                     for(int s=0; s<8; s++) {
                         if (s < hp->stack_depth) {
                             hp->stack_x[s] = (int32_t)(l_stack[s].x * 1000);
                             hp->stack_y[s] = (int32_t)(l_stack[s].y * 1000);
                             hp->stack_angle[s] = (int32_t)(l_stack[s].a * 100);
                         } else {
                             hp->stack_x[s] = 0;
                             hp->stack_y[s] = 0;
                             hp->stack_angle[s] = 0;
                         }
                     }
                     
                     Serial.print("HANDOVER at index ");
                     Serial.print(hp->char_index);
                     Serial.print(" sp=");
                     Serial.println(hp->stack_depth);
                     
                     Udp.beginPacket(serverIP, SERVER_PORT);
                     Udp.write(h_buf, sizeof(h_buf));
                     Udp.endPacket();
                     
                     working = false;
                 }
             }
        }
    }
}
