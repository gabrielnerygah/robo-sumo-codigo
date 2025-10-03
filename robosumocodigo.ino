#include <NewPing.h>

// =================================================================
// CONFIGURAÇÕES DE PINOS (Arduino UNO)
// =================================================================

// --- SENSORES ULTRASSÔNICOS ---
#define TRIG_CENTRO     A0
#define ECHO_CENTRO     A1
#define TRIG_DIREITA    A2
#define ECHO_DIREITA    A3
#define TRIG_ESQUERDA   A4
#define ECHO_ESQUERDA   A5

// --- SENSOR DE LINHA (BORDA) ---
#define PINO_LINHA 7

// --- DRIVER DE MOTOR (L298N) ---
#define VELOCIDADE_A 9  // ENA (PWM)
#define MOTOR_A1     10
#define MOTOR_A2     11
#define VELOCIDADE_B 6  // ENB (PWM)
#define MOTOR_B1     4
#define MOTOR_B2     5

// --- LEDS E BOTÕES ---
#define LED_ROJO     8
#define BOTAO_VERDE  2
#define SWITCH_1     12
#define SWITCH_2     13
#define SWITCH_3     3

// =================================================================
// CONSTANTES DE COMPORTAMENTO
// =================================================================
const int VEL_REPOSO         = 130;  // Velocidade de busca e evasão
const int VELOCIDADE_ATAQUE  = 255;  // Velocidade de ataque
const int DISTANCIA_OPONENTE = 20;   // Ataque Imediato (0cm a 20cm)
const int DISTANCIA_BUSCA    = 40;   // Busca Ativa (20cm a 40cm)
const int DISTANCIA_MAX_NEWPING = 70; // Distância máx para o NewPing ignorar ruídos (70cm)

const int ESTADO_LINHA_PRETA = HIGH; // Lógica da Linha Invertida (HIGH = Preto)
const int TEMPO_RECUO_EVASAO = 150; 
const int TEMPO_GIRO_EVASAO = 500; 

const int TEMPO_ESTRATEGIA_1 = 318;
const int TEMPO_ESTRATEGIA_2 = 127;
const int TEMPO_ESTRATEGIA_3 = 250;

const unsigned long INTERVALO_GIRO_BUSCA = 1500;

// =================================================================
// VARIÁVEIS E OBJETOS
// =================================================================
int velocidadeAtual = VEL_REPOSO;
int estrategia = 1;
int leituraLinha;
float distancia1, distancia2, distancia3;
unsigned long tempoUltimoGiro = 0;
bool proximoGiroEvasivoDireita = true; 

// Objetos NewPing
NewPing sonarCentro(TRIG_CENTRO, ECHO_CENTRO, DISTANCIA_MAX_NEWPING);
NewPing sonarDireita(TRIG_DIREITA, ECHO_DIREITA, DISTANCIA_MAX_NEWPING);
NewPing sonarEsquerda(TRIG_ESQUERDA, ECHO_ESQUERDA, DISTANCIA_MAX_NEWPING);

// =================================================================
// DEBUG E LOGS (Macros Limpas)
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
void lerSensoresUltrassonicos();
void girar(int tempo);
void acaoRecuoGiro(long tempo_ms_recuo, long tempo_ms_giro);
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

  // --- SELEÇÃO DE ESTRATÉGIA ---
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
  aplicarVelocidade(VELOCIDADE_ATAQUE);

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

  // =================================================================
  // LÓGICA DE PRIORIDADE MÁXIMA: 1. BORDA / 2. ATAQUE / 3. BUSCA
  // =================================================================

  // --- 1. DEFESA DE BORDA (PRIORIDADE MÁXIMA) ---
  if (leituraLinha == ESTADO_LINHA_PRETA) {
    acao_log = "BORDA DETECTADA! (MANOBRA EVASIVA)";
    acaoRecuoGiro(TEMPO_RECUO_EVASAO, TEMPO_GIRO_EVASAO); 
    goto log_e_fim; 
  }

  // --- 2. ATAQUE E BUSCA PROXIMA ---
  
  // Ataque Imediato (0cm a 20cm)
  if (distancia1 < DISTANCIA_OPONENTE) {
    velocidadeAtual = VELOCIDADE_ATAQUE; adiantar();
    acao_log = "ATAQUE CENTRO (MAX)";
  }
  // Busca Ativa no Centro (20cm a 40cm)
  else if (distancia1 < DISTANCIA_BUSCA) {
    velocidadeAtual = VEL_REPOSO; 
    adiantar();
    acao_log = "BUSCA ATIVA (Centro)";
  }
  // Curva de Aproximação (Lateral - 0cm a 40cm)
  else if (distancia3 < DISTANCIA_BUSCA) {
    velocidadeAtual = VEL_REPOSO; 
    esquerda(); 
    acao_log = "BUSCA ATIVA (Esquerda)";
  }
  else if (distancia2 < DISTANCIA_BUSCA) {
    velocidadeAtual = VEL_REPOSO; 
    direita(); 
    acao_log = "BUSCA ATIVA (Direita)";
  }
  
  // --- 3. BUSCA PADRÃO (Nenhum oponente visível) ---
  else {
    velocidadeAtual = VEL_REPOSO;
    if (millis() - tempoUltimoGiro > INTERVALO_GIRO_BUSCA) {
      giroEsquerda();
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
// FUNÇÕES ESSENCIAIS
// =================================================================

// Manobra Evasiva de Recuo e Giro Alternado (Contém DELAY)
void acaoRecuoGiro(long tempo_ms_recuo, long tempo_ms_giro) {
    
    // 1. Recuo Rápido
    aplicarVelocidade(VELOCIDADE_ATAQUE); 
    atras(); 
    delay(tempo_ms_recuo);
    parar(); 
    
    // 2. Giro Evasivo (Alternado)
    // CORREÇÃO: Usando VEL_REPOSO
    aplicarVelocidade(VEL_REPOSO); 
    
    if (proximoGiroEvasivoDireita) {
        giroDireita();
        LOG_ESTADO("Giro Evasivo: Direita");
    } else {
        giroEsquerda();
        LOG_ESTADO("Giro Evasivo: Esquerda");
    }
    
    // Inverte a direção para o próximo ciclo
    proximoGiroEvasivoDireita = !proximoGiroEvasivoDireita; 
    
    // Executa o giro (interrompível se achar oponente)
    girar(tempo_ms_giro); 
}

// Usa a biblioteca NewPing
void lerSensoresUltrassonicos() {
  // CORREÇÃO: Usando DISTANCIA_MAX_NEWPING no lugar de DISTANCIA_MAX_BUSCA
  int d1 = sonarCentro.ping_cm();
  int d2 = sonarDireita.ping_cm();
  int d3 = sonarEsquerda.ping_cm();
  
  // Converte 0 (fora do alcance do NewPing) para a distância máxima de busca + 1
  distancia1 = (d1 == 0) ? DISTANCIA_BUSCA + 1 : (float)d1;
  distancia2 = (d2 == 0) ? DISTANCIA_BUSCA + 1 : (float)d2;
  distancia3 = (d3 == 0) ? DISTANCIA_BUSCA + 1 : (float)d3;

  // Filtra leituras que caíram dentro do limite do NewPing, mas são maiores que o DISTANCIA_BUSCA
  if (distancia1 > DISTANCIA_BUSCA) distancia1 = DISTANCIA_BUSCA + 1;
  if (distancia2 > DISTANCIA_BUSCA) distancia2 = DISTANCIA_BUSCA + 1;
  if (distancia3 > DISTANCIA_BUSCA) distancia3 = DISTANCIA_BUSCA + 1;
}

// Gira por um tempo definido (usado para Estratégia Inicial e Busca)
void girar(int tempo) {
  unsigned long inicio = millis();
  while (millis() - inicio < tempo) {
    lerSensoresUltrassonicos();
    // Interrompe o giro se detectar oponente
    if (distancia1 < DISTANCIA_BUSCA || distancia2 < DISTANCIA_BUSCA || distancia3 < DISTANCIA_BUSCA) {
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