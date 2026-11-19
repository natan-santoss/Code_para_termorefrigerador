
#include <Arduino.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <DHTesp.h>

const uint8_t NUM_SENSORES = 5;

const uint8_t DHT22_INPUT1_PIN = 14;
const uint8_t DHT22_INPUT2_PIN = 12;
const uint8_t DHT22_INPUT3_PIN = 13;
const uint8_t DHT22_INPUT4_PIN = 19;   
const uint8_t DHT22_INPUT5_PIN = 23;   

const uint8_t LED_PIN_1 = 27;
const uint8_t LED_PIN_2 = 26;
const uint8_t LED_PIN_3 = 25;
const uint8_t LED_PIN_4 = 33;
const uint8_t LED_PIN_5 = 32;
const uint8_t LED_PIN_REFRIGERADOR = 3;   

const uint8_t MOTOR_PWM_PIN_1 = 15;

const uint8_t LCD_SCL = 22;
const uint8_t LCD_SDA = 21;

const uint8_t PINOS_DHT[NUM_SENSORES] =
  {DHT22_INPUT1_PIN, DHT22_INPUT2_PIN, DHT22_INPUT3_PIN, DHT22_INPUT4_PIN, DHT22_INPUT5_PIN};
const uint8_t PINOS_LED_SENSOR[NUM_SENSORES] =
  {LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4, LED_PIN_5};

const uint8_t KEYBOARD_ROWS = 4;
const uint8_t KEYBOARD_COLS = 3;

char keys[KEYBOARD_ROWS][KEYBOARD_COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};

byte pinoLinhas[KEYBOARD_ROWS] = {2, 0, 4, 16};
byte pinoColunas[KEYBOARD_COLS] = {17, 5, 18};

const int   TEMP_MIN = 18;
const int   TEMP_MAX = 26;
const float AMPLITUDE_HOMOGENEA = 3.0;  
const float MARGEM_TROCA = 0.5;         
const int   ANGULO_CENTRO = 90;
const int   ANGULOS_SENSOR[NUM_SENSORES] = {0, 45, 90, 135, 180};
const uint32_t PERIODO_LEITURA_MS = 2000;

enum TipoEvento : uint8_t { EV_TECLA, EV_RESET, EV_LEITURA };

struct Evento {
  TipoEvento tipo;
  char  tecla;                    
  float temps[NUM_SENSORES];      
};

class FilaEventos {
  QueueHandle_t fila = nullptr;
public:
  void iniciar(size_t tamanho) { fila = xQueueCreate(tamanho, sizeof(Evento)); }
  bool enqueue(const Evento &e) { return xQueueSend(fila, &e, 0) == pdTRUE; }
  Evento dequeue() {                         
    Evento e;
    xQueueReceive(fila, &e, portMAX_DELAY);
    return e;
  }
  bool peek(Evento &e) { return xQueuePeek(fila, &e, 0) == pdTRUE; }
};


class SensorTemperatura {
  DHTesp dht;
public:
  uint8_t  pino = 0;
  float    ultimaLeitura = NAN;
  uint32_t instanteUltimaLeitura = 0;

  void iniciar(uint8_t pin) {
    pino = pin;
    dht.setup(pino, DHTesp::DHT22);
  }
  float lerTemperatura() {
    ultimaLeitura = dht.getTemperature();   
    instanteUltimaLeitura = millis();
    return ultimaLeitura;
  }
};


class LED {
  uint8_t pino = 0;
public:
  void iniciar(uint8_t pin) { pino = pin; pinMode(pino, OUTPUT); apagar(); }
  void acender() { digitalWrite(pino, HIGH); }
  void apagar()  { digitalWrite(pino, LOW); }
};

class Refrigerador {
  Servo servo;
  bool ligado = false;
  int anguloAtual = -1;
public:
  void iniciar(uint8_t pin) {
    servo.setPeriodHertz(50);
    servo.attach(pin, 500, 2400);
    direcionar(ANGULO_CENTRO);
    desligar();
  }
  void direcionar(int angulo) {
    if (angulo != anguloAtual) {           
      servo.write(angulo);
      anguloAtual = angulo;
    }
  }
  void ligar(){ ligado = true; }
  void desligar() { ligado = false; }
  bool estaLigado() { return ligado; }
};


class Display {
  LiquidCrystal_I2C lcd{0x27, 20, 4};
public:
  void iniciar() { lcd.init(); lcd.backlight(); }
  void limpar() { lcd.clear(); }
  void piscar(bool ativo) { if (ativo) lcd.blink(); else lcd.noBlink(); }

  void perguntarTemperatura() {
    limpar();
    lcd.setCursor(0, 0); lcd.print("Qual a temperatura");
    lcd.setCursor(0, 1); lcd.print("desejada? (18 a 26)");
    lcd.setCursor(0, 3); lcd.print("#=confirma *=apaga");
    exibirEntrada("");
    piscar(true);
  }
  void exibirEntrada(const char *teclas) {
    lcd.setCursor(0, 2);
    lcd.print("                    ");
    lcd.setCursor(0, 2);
    lcd.print(teclas);                     
  }
  void temperaturaInvalida(const char *mensagem) {
    piscar(false);
    limpar();
    lcd.print(mensagem);
  }
  void exibirTemperaturas(int desejada, float media) {
    char l0[21], l1[21];
    snprintf(l0, sizeof l0, "Temp. desejada: %2dC ", desejada);
    if (isnan(media)) snprintf(l1, sizeof l1, "Temp. media: --     ");
    else              snprintf(l1, sizeof l1, "Temp. media: %4.1fC  ", media);
    lcd.setCursor(0, 0); lcd.print(l0);
    lcd.setCursor(0, 1); lcd.print(l1);
  }
};


class Teclado {
  Keypad *customKeypad = nullptr;
  FilaEventos *fila = nullptr;
  char teclasPressionada[3] = "";
  uint8_t teclas_armazenadas = 0;

  bool asteriscoEsperando = false;          // já apertaram um * e estamos esperando o segundo?
  unsigned long horaDoAsterisco = 0;        // quando o primeiro * foi apertado
  const unsigned long TEMPO_DUPLO = 400;    // ms para o segundo * contar como reset

  void enviar(TipoEvento tipo, char tecla = 0) {
    Evento e{};
    e.tipo = tipo;
    e.tecla = tecla;
    fila->enqueue(e);
  }

public:
  void iniciar(byte *linhas, byte *colunas, FilaEventos *f) {
    customKeypad = new Keypad(makeKeymap(keys), linhas, colunas, KEYBOARD_ROWS, KEYBOARD_COLS);
    fila = f;
    limpar();
  }

  char lerTecla() {
    char ultima = NO_KEY;

    if (asteriscoEsperando && millis() - horaDoAsterisco > TEMPO_DUPLO) {
      asteriscoEsperando = false;
      enviar(EV_TECLA, '*');
      ultima = '*';
    }

    if (!customKeypad->getKeys()) return ultima;

    for (int i = 0; i < LIST_MAX; i++) {
      Key &k = customKeypad->key[i];
      if (!(k.stateChanged && k.kstate == RELEASED)) continue;

      if (k.kchar == '*') {
        if (asteriscoEsperando) {
          // segundo * a tempo: reset
          asteriscoEsperando = false;
          enviar(EV_RESET);
        } else {
          asteriscoEsperando = true;
          horaDoAsterisco = millis();
        }
        continue;
      }
      if (asteriscoEsperando) {
        asteriscoEsperando = false;
        enviar(EV_TECLA, '*');
      }
      enviar(EV_TECLA, k.kchar);
      ultima = k.kchar;
    }
    return ultima;
  }

  void adicionarDigito(char d) {
    if (teclas_armazenadas < 2) { teclasPressionada[teclas_armazenadas++] = d; teclasPressionada[teclas_armazenadas] = '\0'; }
  }
  void apagarDigito() {
    if (teclas_armazenadas > 0) teclasPressionada[--teclas_armazenadas] = '\0';
  }
  int lerValor() {
    return (teclas_armazenadas == 2) ? atoi(teclasPressionada) : -1;
  }
  int confirmar() {
    int v = lerValor();
    limpar();
    return v;
  }
  void limpar() { teclas_armazenadas = 0; teclasPressionada[0] = '\0'; }
  const char *texto() { return teclasPressionada; }
};


class ControleTemperatura {
  float temps[NUM_SENSORES];
  int desejada = 0;
  int regiaoAtual = -1;                    
public:
  void iniciar() {
    desejada = 0;
    regiaoAtual = -1;
    for (auto &t : temps) t = NAN;
  }
  bool temperaturaValida(int valor) { return valor >= TEMP_MIN && valor <= TEMP_MAX; }
  void updateGoalTemp(int novadesejada) { desejada = novadesejada; }
  int  lerDesejada()                { return desejada; }
  void atualizarLeituras(const float *novas) {
    for (int i = 0; i < NUM_SENSORES; i++) temps[i] = novas[i];
  }

  float calcularMedia() {
    float soma = 0; int validos = 0;
    for (float t : temps) if (!isnan(t)) { soma += t; validos++; }
    return validos ? soma / validos : NAN;
  }
  int sensorMaisQuente() {                  
    int idx = -1;
    for (int i = 0; i < NUM_SENSORES; i++)
      if (!isnan(temps[i]) && (idx < 0 || temps[i] > temps[idx])) idx = i;
    return idx;
  }
  float calcularAmplitude() {
    float mx = -1000, mn = 1000; int validos = 0;
    for (float t : temps) if (!isnan(t)) { mx = max(mx, t); mn = min(mn, t); validos++; }
    return (validos >= 2) ? mx - mn : 0;
  }
  bool necessitaRefrigeracao() {
    float m = calcularMedia();
    return !isnan(m) && m > desejada;
  }
  
  int calcularAngulo() {
    int quente = sensorMaisQuente();
    if (quente < 0 || calcularAmplitude() < AMPLITUDE_HOMOGENEA) {
      regiaoAtual = -1;
      return ANGULO_CENTRO;
    }
    if (regiaoAtual < 0 || isnan(temps[regiaoAtual]) ||
        temps[quente] - temps[regiaoAtual] >= MARGEM_TROCA) {
      regiaoAtual = quente;
    }
    return ANGULOS_SENSOR[regiaoAtual];
  }
};


FilaEventos         fila;
SensorTemperatura   sensores[NUM_SENSORES];
LED                 ledsSensores[NUM_SENSORES];
LED                 ledRefrigerador;
Refrigerador        refrigerador;
Display             display;
Teclado             teclado;
ControleTemperatura controle;


enum Estado : uint8_t {
  AGUARDANDO_TEMPERATURA,   
  REFRIGERADOR_DESLIGADO,   
  REFRIGERADOR_LIGADO,      
  NUM_ESTADOS
};

enum EventoMaquina : uint8_t {
  E_DIGITO,
  E_APAGAR,                 
  E_CONFIRMA_VALIDO,       
  E_CONFIRMA_INVALIDO,      
  E_RESET,                  
  E_LEITURA_NORMAL,         
  E_LEITURA_QUENTE,         
  NUM_EVENTOS,
  E_NENHUM                  
};

enum Acao : uint8_t {
  A_NENHUMA,
  A_ADICIONAR_DIGITO,
  A_APAGAR_DIGITO,
  A_REGISTRAR_TEMPERATURA,
  A_TENTE_DE_NOVO,
  A_RESETAR,
  A_ATUALIZAR_MONITORAMENTO,
  A_LIGAR_REFRIGERADOR,
  A_DIRECIONAR_REFRIGERADOR,
  A_DESLIGAR_REFRIGERADOR
};

Acao matrizAcao[NUM_ESTADOS][NUM_EVENTOS];

void iniciaMatrizAcao(void) {
  matrizAcao[AGUARDANDO_TEMPERATURA][E_DIGITO] = A_ADICIONAR_DIGITO;
  matrizAcao[AGUARDANDO_TEMPERATURA][E_APAGAR]= A_APAGAR_DIGITO;
  matrizAcao[AGUARDANDO_TEMPERATURA][E_CONFIRMA_VALIDO]   = A_REGISTRAR_TEMPERATURA;
  matrizAcao[AGUARDANDO_TEMPERATURA][E_CONFIRMA_INVALIDO] = A_TENTE_DE_NOVO;
  matrizAcao[AGUARDANDO_TEMPERATURA][E_RESET]= A_RESETAR;

  matrizAcao[REFRIGERADOR_DESLIGADO][E_RESET]= A_RESETAR;
  matrizAcao[REFRIGERADOR_DESLIGADO][E_LEITURA_NORMAL]=A_ATUALIZAR_MONITORAMENTO;
  matrizAcao[REFRIGERADOR_DESLIGADO][E_LEITURA_QUENTE] = A_LIGAR_REFRIGERADOR;

  matrizAcao[REFRIGERADOR_LIGADO][E_RESET] = A_RESETAR;
  matrizAcao[REFRIGERADOR_LIGADO][E_LEITURA_NORMAL] = A_DESLIGAR_REFRIGERADOR;
  matrizAcao[REFRIGERADOR_LIGADO][E_LEITURA_QUENTE] = A_DIRECIONAR_REFRIGERADOR;
}

Estado matrizProximoEstado[NUM_ESTADOS][NUM_EVENTOS];

void iniciaMatrizProximoEstado(void) {
  matrizProximoEstado[AGUARDANDO_TEMPERATURA][E_CONFIRMA_VALIDO] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[AGUARDANDO_TEMPERATURA][E_RESET] = AGUARDANDO_TEMPERATURA;

  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_DIGITO] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_APAGAR] =REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_CONFIRMA_VALIDO] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_CONFIRMA_INVALIDO] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_RESET] = AGUARDANDO_TEMPERATURA;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_LEITURA_NORMAL] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_DESLIGADO][E_LEITURA_QUENTE]= REFRIGERADOR_LIGADO;

  matrizProximoEstado[REFRIGERADOR_LIGADO][E_DIGITO]= REFRIGERADOR_LIGADO;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_APAGAR] = REFRIGERADOR_LIGADO;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_CONFIRMA_VALIDO]  = REFRIGERADOR_LIGADO;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_CONFIRMA_INVALIDO] = REFRIGERADOR_LIGADO;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_RESET] = AGUARDANDO_TEMPERATURA;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_LEITURA_NORMAL] = REFRIGERADOR_DESLIGADO;
  matrizProximoEstado[REFRIGERADOR_LIGADO][E_LEITURA_QUENTE]  = REFRIGERADOR_LIGADO;
}

const char *NOME_ESTADO[NUM_ESTADOS] = {"AGUARDANDO_TEMPERATURA", "REFRIGERADOR_DESLIGADO", "REFRIGERADOR_LIGADO"};


class Controlador {
  Estado estado = AGUARDANDO_TEMPERATURA;
  char teclaAtual = 0;

  
  EventoMaquina obterEvento(const Evento &ev) {
    switch (ev.tipo) {
      case EV_RESET:
        return E_RESET;
      case EV_TECLA:
        teclaAtual = ev.tecla;
        if (ev.tecla >= '0' && ev.tecla <= '9') return E_DIGITO;
        if (ev.tecla == '*') return E_APAGAR;
        if (ev.tecla == '#')
          return controle.temperaturaValida(teclado.lerValor()) ? E_CONFIRMA_VALIDO
                                                                : E_CONFIRMA_INVALIDO;
        return E_NENHUM;
      case EV_LEITURA:
        controle.atualizarLeituras(ev.temps);
        return controle.necessitaRefrigeracao() ? E_LEITURA_QUENTE : E_LEITURA_NORMAL;
    }
    return E_NENHUM;
  }

  void atualizarLedsSensores() {
    int quente = controle.sensorMaisQuente();
    for (int i = 0; i < NUM_SENSORES; i++) {
      if (i == quente) ledsSensores[i].acender();
      else             ledsSensores[i].apagar();
    }
  }

  void mostrarMonitoramento() {
    display.exibirTemperaturas(controle.lerDesejada(), controle.calcularMedia());
    atualizarLedsSensores();
  }

  void executarAcao(Acao a) {
    switch (a) {
      case A_NENHUMA:
        break;
      case A_ADICIONAR_DIGITO:
        teclado.adicionarDigito(teclaAtual);
        display.exibirEntrada(teclado.texto());
        break;
      case A_APAGAR_DIGITO:
        teclado.apagarDigito();
        display.exibirEntrada(teclado.texto());
        break;
      case A_REGISTRAR_TEMPERATURA:
        controle.updateGoalTemp(teclado.confirmar());
        display.piscar(false);
        display.limpar();
        display.exibirTemperaturas(controle.lerDesejada(), NAN);
        break;
      case A_TENTE_DE_NOVO:
        display.temperaturaInvalida("Tente de novo");
        vTaskDelay(pdMS_TO_TICKS(1500));
        teclado.limpar();
        display.perguntarTemperatura();
        break;
      case A_RESETAR:
        reset();
        break;
      case A_ATUALIZAR_MONITORAMENTO:
        mostrarMonitoramento();
        break;
      case A_LIGAR_REFRIGERADOR:
        refrigerador.ligar();
        ledRefrigerador.acender();
        refrigerador.direcionar(controle.calcularAngulo());
        mostrarMonitoramento();
        break;
      case A_DIRECIONAR_REFRIGERADOR:
        refrigerador.direcionar(controle.calcularAngulo());
        mostrarMonitoramento();
        break;
      case A_DESLIGAR_REFRIGERADOR:
        refrigerador.desligar();
        ledRefrigerador.apagar();
        mostrarMonitoramento();
        break;
    }
  }

public:
  void iniciar() { reset(); }

  void reset() {
    refrigerador.desligar();
    refrigerador.direcionar(ANGULO_CENTRO);
    ledRefrigerador.apagar();
    for (auto &l : ledsSensores) l.apagar();
    controle.iniciar();
    teclado.limpar();
    display.perguntarTemperatura();
  }

  void alternarEstado(Estado novo) {
    if (novo != estado) Serial.printf("[estado] %s -> %s\n", NOME_ESTADO[estado], NOME_ESTADO[novo]);
    estado = novo;
  }

  void tratarEvento(const Evento &ev) {
    EventoMaquina e = obterEvento(ev);
    if (e == E_NENHUM) return;
    executarAcao(matrizAcao[estado][e]);
    alternarEstado(matrizProximoEstado[estado][e]);
  }

  void loop() {
    Evento ev = fila.dequeue();
    tratarEvento(ev);
  }
};

Controlador controlador;


void keypadTask(void *) {
  for (;;) {  // laço infinito
    teclado.lerTecla();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void tarefaSensores(void *) {
  TickType_t ultimo = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(PERIODO_LEITURA_MS));
    Evento ev{};
    ev.tipo = EV_LEITURA;
    for (int i = 0; i < NUM_SENSORES; i++) ev.temps[i] = sensores[i].lerTemperatura();
    fila.enqueue(ev);
  }
}

void tarefaControlador(void *) {
  for (;;) controlador.loop();
}


void setup() {
  
  Serial.begin(115200, SERIAL_8N1, -1, 1);
  Wire.begin(LCD_SDA, LCD_SCL);

  fila.iniciar(10);
  for (int i = 0; i < NUM_SENSORES; i++) {
    sensores[i].iniciar(PINOS_DHT[i]);
    ledsSensores[i].iniciar(PINOS_LED_SENSOR[i]);
  }
  ledRefrigerador.iniciar(LED_PIN_REFRIGERADOR);
  refrigerador.iniciar(MOTOR_PWM_PIN_1);
  display.iniciar();
  teclado.iniciar(pinoLinhas, pinoColunas, &fila);
  controle.iniciar();
  controlador.iniciar();

  iniciaMatrizAcao();
  iniciaMatrizProximoEstado();

  xTaskCreate(keypadTask,     "teclado",     2048, NULL, 2, NULL);
  xTaskCreate(tarefaSensores,    "sensores",    4096, NULL, 1, NULL);
  xTaskCreate(tarefaControlador, "controlador", 4096, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(portMAX_DELAY);   
}