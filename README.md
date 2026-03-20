# esp32-distributed-fractals
Rozproszony generator fraktali (L-systemy) w architekturze klient-serwer. Serwer w C (Linux) zarządza klastrem węzłów IoT (ESP32/Arduino). Węzły równolegle renderują grafikę ASCII (Turtle Graphics). Projekt wdraża autorski protokół sieciowy UDP (ALP) oraz zaawansowany mechanizm przekazywania stanu obliczeń (handover) między mikrokontrolerami.
