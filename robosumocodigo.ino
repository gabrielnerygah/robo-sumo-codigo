// =================================================================
// CONFIGURAÇÕES (Arduino UNO)
// =================================================================

// --- SENSORES ULTRASSÔNICOS ---
#define TRIG_CENTRO     A0
#define ECHO_CENTRO     A1
#define TRIG_DIREITA    A2
#define ECHO_DIREITA    A3
#define TRIG_ESQUERDA   A4
#define ECHO_ESQUERDA   A5

// --- SENSOR DE LINHA ---
#define PINO_LINHA 7

// --- DRIVER DE MOTOR (L298N) ---
#define VELOCIDADE_A 9
#define MOTOR_A1     10
#define MOTOR_A2     11
#define VELOCIDADE_B 6
#define MOTOR_B1     4
#define MOTOR_B2     5

// --- LEDS E BOTÕES ---
#define LED_ROJO     8
#define BOTAO_VERDE  2
#define SWITCH_1     12
#define SWITCH_2     13
#define SWITCH_3     3

// =================================================================
// CONSTANTES
// =================================================================
const int VEL_REPOSO     = 130;
const int VEL_MAXIMA     = 255;
const int DISTANCIA_OPONENTE = 30;
const int ESTADO_LINHA_PRETA = HIGH; // <<< CORREÇÃO APLICADA AQUI (HIGH = Preto)
const int TEMPO_RECUO    = 150;
const int TEMPO_GIRO_LINHA = 300;
const int TEMPO_ESTRATEGIA_1 = 318;
const int TEMPO_ESTRATEGIA_2 = 127;
const int TEMPO_ESTRATEGIA_3 = 250;

const unsigned long INTERVALO_GIRO_BUSCA = 2000;

// =================================================================
// VARIÁVEIS
// =================================================================
int velocidadeAtual = VEL_REPOSO;
int estrategia = 1;
int leituraLinha;
float distancia1, distancia2, distancia3;
unsigned long tempoUltimoGiro = 0;
bool giroAtivo = false;

// =================================================================
// DEBUG E LOGS (Macros de Debug Corrigidas)
// =================================================================
#define DEBUG 1
#if DEBUG
  #define LOG_INIT(x) Serial.println(x)
  #define LOG_ESTADO(x) Serial.println(x)
  #define LOG_VALOR(x,y) {Serial.print(x); Serial.println(y);}
  #define LOG_NO_LOOP(vel, linha, d1, d2, d3, acao) { \
    Serial.print("Vel:"); Serial.print(vel); \
    Serial.print(" | Linha:"); Serial.print(linha); \
    Serial.print(" | US: C="); Serial.print(d1); \
    Serial.print(" D="); Serial.print(d2); \
    Serial.print(" E="); Serial.print(d3); \
    Serial.print(" | ACAO: "); Serial.println(acao); \
  }
#else
  #define LOG_INIT(x)
  #define LOG_ESTADO(x)
  #define LOG_VALOR(x,y)
  #define LOG_NO_LOOP(vel, linha, d1, d2, d3, acao)
#endif


// =================================================================
// ENUMS E PROTÓTIPOS
// =================================================================
enum Estrategia { E1 = 1, E2, E3 };

float lerDistancia(int trigPin, int echoPin);
void lerSensoresUltrassonicos();
void girar(int tempo);
void aplicarVelocidade(int vel);
void adiantar(), atras(), direita(), esquerda(), giroEsquerda(), giroDireita(), parar();

// =================================================================
// SETUP
// =================================================================
void setup() {
  Serial.begin(9600);
  LOG_INIT("--- Mini Sumo Inicializando ---");

  // Configuração dos pinos
  pinMode(TRIG_CENTRO, OUTPUT);   pinMode(ECHO_CENTRO, INPUT);
  pinMode(TRIG_DIREITA, OUTPUT); pinMode(ECHO_DIREITA, INPUT);
  pinMode(TRIG_ESQUERDA, OUTPUT); pinMode(ECHO_ESQUERDA, INPUT);

  pinMode(PINO_LINHA, INPUT);

  pinMode(VELOCIDADE_A, OUTPUT); pinMode(MOTOR_A1, OUTPUT); pinMode(MOTOR_A2, OUTPUT);
  pinMode(VELOCIDADE_B, OUTPUT); pinMode(MOTOR_B1, OUTPUT); pinMode(MOTOR_B2, OUTPUT);

  pinMode(LED_ROJO, OUTPUT);
  pinMode(BOTAO_VERDE, INPUT);
  pinMode(SWITCH_1, INPUT_PULLUP);
  pinMode(SWITCH_2, INPUT_PULLUP);
  pinMode(SWITCH_3, INPUT_PULLUP);

  // Seleção de Estratégia
  LOG_INIT("Aguardando selecao de estrategia...");
  while (digitalRead(BOTAO_VERDE) == LOW) {
    digitalWrite(LED_ROJO, HIGH);
    if (!digitalRead(SWITCH_1)) estrategia = E1;
    if (!digitalRead(SWITCH_2)) estrategia = E2;
    if (!digitalRead(SWITCH_3)) estrategia = E3;
  }

  LOG_VALOR("Estrategia Selecionada: ", estrategia);

  while (digitalRead(BOTAO_VERDE) == HIGH);
  LOG_INIT("Contagem regressiva de 5 segundos...");

  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_ROJO, HIGH); delay(500);
    digitalWrite(LED_ROJO, LOW);  delay(500);
  }

  digitalWrite(LED_ROJO, HIGH);
  aplicarVelocidade(VEL_MAXIMA);

  LOG_VALOR("Executando estrategia inicial: ", estrategia);

  // Execução da Estratégia Inicial
  switch (estrategia) {
    case E1: girar(TEMPO_ESTRATEGIA_1); adiantar(); break;
    case E2: girar(TEMPO_ESTRATEGIA_2); adiantar(); break;
    case E3: adiantar(); delay(TEMPO_ESTRATEGIA_3); break;
  }
}

// =================================================================
// LOOP PRINCIPAL
// =================================================================
void loop() {
  lerSensoresUltrassonicos();
  leituraLinha = digitalRead(PINO_LINHA);
  
  const char* acao_log; 

  // --- 1. DETECÇÃO DE BORDA (Prioridade Máxima) ---
  if (leituraLinha == ESTADO_LINHA_PRETA) {
    acao_log = "BORDA DETECTADA! (Recuo + Giro)";
    aplicarVelocidade(VEL_REPOSO);
    atras(); delay(TEMPO_RECUO);
    girar(TEMPO_GIRO_LINHA);
    goto log_e_fim; 
  }

  // --- 2. ATAQUE (Prioridade Média) ---
  if (distancia1 < DISTANCIA_OPONENTE) {
    velocidadeAtual = VEL_MAXIMA; adiantar();
    acao_log = "ATAQUE CENTRO";
  }
  else if (distancia3 < DISTANCIA_OPONENTE) {
    velocidadeAtual = VEL_MAXIMA; esquerda();
    acao_log = "ATAQUE ESQUERDA";
  }
  else if (distancia2 < DISTANCIA_OPONENTE) {
    velocidadeAtual = VEL_MAXIMA; direita();
    acao_log = "ATAQUE DIREITA";
  }
  // --- 3. BUSCA (Prioridade Mínima) ---
  else {
    velocidadeAtual = VEL_REPOSO;
    if (millis() - tempoUltimoGiro > INTERVALO_GIRO_BUSCA) {
      girar(500); 
      tempoUltimoGiro = millis();
      acao_log = "BUSCA (Giro)";
    } else {
      adiantar();
      acao_log = "BUSCA (Avanco Lento)";
    }
  }
  
  aplicarVelocidade(velocidadeAtual);

// ======================= LOG DO ESTADO =======================
log_e_fim:
#if DEBUG
  LOG_NO_LOOP(velocidadeAtual, leituraLinha, distancia1, distancia2, distancia3, acao_log);
#endif
}

// =================================================================
// FUNÇÕES AUXILIARES
// =================================================================
float lerDistancia(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duracao = pulseIn(echoPin, HIGH, 20000); 
  return (duracao > 0) ? duracao * 0.034 / 2 : 999; 
}

void lerSensoresUltrassonicos() {
  distancia1 = lerDistancia(TRIG_CENTRO, ECHO_CENTRO);
  distancia2 = lerDistancia(TRIG_DIREITA, ECHO_DIREITA);
  distancia3 = lerDistancia(TRIG_ESQUERDA, ECHO_ESQUERDA);
}

void girar(int tempo) {
  giroEsquerda();
  unsigned long inicio = millis();
  while (millis() - inicio < tempo) {
    lerSensoresUltrassonicos();
    if (distancia1 < DISTANCIA_OPONENTE || distancia2 < DISTANCIA_OPONENTE || distancia3 < DISTANCIA_OPONENTE) {
      parar(); return; 
    }
  }
}

void aplicarVelocidade(int vel) {
  analogWrite(VELOCIDADE_A, vel);
  analogWrite(VELOCIDADE_B, vel);
}

// =================================================================
// FUNÇÕES DE MOVIMENTO
// =================================================================
void adiantar()    { digitalWrite(MOTOR_A1, HIGH); digitalWrite(MOTOR_A2, LOW);  digitalWrite(MOTOR_B1, HIGH); digitalWrite(MOTOR_B2, LOW); }
void atras()       { digitalWrite(MOTOR_A1, LOW);  digitalWrite(MOTOR_A2, HIGH); digitalWrite(MOTOR_B1, LOW);  digitalWrite(MOTOR_B2, HIGH); }
void direita()     { digitalWrite(MOTOR_A1, HIGH); digitalWrite(MOTOR_A2, LOW);  digitalWrite(MOTOR_B1, LOW);  digitalWrite(MOTOR_B2, HIGH); }
void esquerda()    { digitalWrite(MOTOR_A1, LOW);  digitalWrite(MOTOR_A2, HIGH); digitalWrite(MOTOR_B1, HIGH); digitalWrite(MOTOR_B2, LOW); }
void giroEsquerda(){ digitalWrite(MOTOR_A1, LOW);  digitalWrite(MOTOR_A2, HIGH); digitalWrite(MOTOR_B1, HIGH); digitalWrite(MOTOR_B2, LOW); }
void giroDireita() { digitalWrite(MOTOR_A1, HIGH); digitalWrite(MOTOR_A2, LOW);  digitalWrite(MOTOR_B1, LOW);  digitalWrite(MOTOR_B2, HIGH); }
void parar()       { digitalWrite(MOTOR_A1, LOW);  digitalWrite(MOTOR_A2, LOW);  digitalWrite(MOTOR_B1, LOW);  digitalWrite(MOTOR_B2, LOW); }