/**
 * -------------------------------------------------------------------
 * Sistema de Controle de Acesso IoT - Módulo Cliente (Arduino)
 * -------------------------------------------------------------------
 * Este código transforma o Arduino em um cliente burro/escravo. 
 * Sua única função é ler os sensores (RFID e Ultrassônico), empacotar 
 * os dados em formato JSON e enviar pela porta Serial. 
 * Em seguida, ele aguarda comandos JSON do Servidor (ex: Raspberry Pi) 
 * para acionar os atuadores (LEDs e Buzzer).
 * -------------------------------------------------------------------
 */

#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>

// --- Definição dos Pinos do Hardware ---
#define SS_PIN      10  // Pino Slave Select do RFID
#define RST_PIN     9   // Pino de Reset do RFID
#define TRIG_PIN    7   // Pino Trigger do Ultrassônico (emite o som)
#define ECHO_PIN    6   // Pino Echo do Ultrassônico (ouve o som)
#define LED_VERDE   4   // Indicador de Acesso Liberado
#define LED_VERMELHO 5  // Indicador de Acesso Negado
#define BUZZER      3   // Emissor de alertas sonoros

// Inicializa a instância do leitor RFID
MFRC522 rfid(SS_PIN, RST_PIN);

void setup() {
  // Configura a velocidade da comunicação com a Raspberry Pi (deve ser a mesma no Python/Node)
  Serial.begin(9600);
  
  // Inicia os barramentos de comunicação
  SPI.begin();
  rfid.PCD_Init();
  
  // Configura os pinos de entrada e saída
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Garante que o sistema inicie com os atuadores desligados
  desligarLeds();
  
  // A macro F() armazena esse texto na memória Flash (ROM) em vez da RAM, economizando recursos
  Serial.println(F("Arduino Pronto! Aguardando servidor..."));
}

void loop() {
  // A função principal (loop) deve ser limpa e delegar tarefas para módulos específicos
  int distancia = medirDistancia();
  verificarRFID(distancia);
  processarComandosServidor();
}

// ==========================================
// MÓDULOS DE SENSORES
// ==========================================

/**
 * Dispara o sensor ultrassônico e calcula a distância em centímetros.
 */
int medirDistancia() {
  // Gera um pulso curto de 10 microssegundos para acionar o ultrassônico
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Lê o tempo de retorno do som.
  // O parâmetro 30000 (30ms) é o Timeout: evita que o Arduino trave caso não haja barreira na frente.
  long duracao = pulseIn(ECHO_PIN, HIGH, 30000); 
  
  if (duracao == 0) return 0; // Se deu timeout (0), retorna 0 (fora de alcance)
  
  // Calcula a distância: Velocidade do som / 2 (ida e volta)
  return duracao * 0.034 / 2;
}

/**
 * Verifica se há um cartão na leitora. Se houver, extrai a UID de forma segura
 * e repassa para a função de envio JSON.
 */
void verificarRFID(int distancia) {
  // Retorna imediatamente se não houver um cartão ou se a leitura falhar
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return; 
  }

  // Declara um array de caracteres com 32 espaços. 
  // Isso protege o Arduino de estouro de memória caso um cartão gigante (7 bytes) seja lido.
  char uidLida[32] = ""; 
  
  // Monta a string da UID convertendo os bytes em formato Hexadecimal
  for (byte i = 0; i < rfid.uid.size; i++) {
    char buffer[4];
    // Se for o primeiro byte, não coloca espaço. Nos próximos, coloca um espaço antes.
    sprintf(buffer, i == 0 ? "%02X" : " %02X", rfid.uid.uidByte[i]);
    strcat(uidLida, buffer); // Junta ao texto final
  }
  
  enviarDadosJSON(uidLida, distancia);
  
  // Coloca o cartão lido em "hibernação" para não gerar leituras repetidas em loop
  rfid.PICC_HaltA(); 
  delay(500); 
}

// ==========================================
// COMUNICAÇÃO JSON
// ==========================================

/**
 * Empacota a UID e a Distância em um formato JSON padrão e envia pela Serial.
 * Exemplo gerado: {"uid":"01 02 03 04", "distancia":120}
 */
void enviarDadosJSON(const char* uid, int distancia) {
  // Aloca 128 bytes estáticos para montar o JSON (tamanho ideal para essa quantidade de dados)
  StaticJsonDocument<128> docEnvio; 
  
  docEnvio["uid"] = uid;
  docEnvio["distancia"] = distancia;

  // Transforma o objeto JSON em texto e imprime na Serial
  serializeJson(docEnvio, Serial);
  Serial.println(); // Envia uma quebra de linha ('\n') para o servidor saber que a mensagem acabou
}

/**
 * Escuta a porta Serial procurando por comandos enviados pelo Servidor (Raspberry Pi).
 * O comando deve vir no formato: {"comando": "ACAO"}
 */
void processarComandosServidor() {
  if (Serial.available() == 0) return; // Sai se não houver nada no cabo USB

  // Lê a mensagem inteira até encontrar a quebra de linha
  String jsonRecebido = Serial.readStringUntil('\n');
  StaticJsonDocument<128> docRecebido;
  
  // Tenta decodificar o texto para JSON
  DeserializationError erro = deserializeJson(docRecebido, jsonRecebido);
  if (erro) return; // Se for um texto sujo ou JSON quebrado, ignora a mensagem silenciosamente
  
  // Extrai o valor associado à chave "comando"
  const char* comando = docRecebido["comando"]; 
  if (!comando) return; // Se a chave não existir, ignora

  executarAcao(comando);
}

// ==========================================
// AÇÕES E FEEDBACK VISUAL/SONORO
// ==========================================

/**
 * Recebe o texto do comando validado e escolhe qual função visual/sonora ativar.
 */
void executarAcao(const char* comando) {
  // A função strcmp() compara strings em C/C++. Se retornar 0, os textos são idênticos.
  if (strcmp(comando, "APROVADO") == 0) {
    sinalizarSucesso();
  } else if (strcmp(comando, "NEGADO") == 0) {
    sinalizarErro();
  } else if (strcmp(comando, "MODO_CADASTRO") == 0) {
    sinalizarModoCadastro();
  } else if (strcmp(comando, "CADASTRO_OK") == 0) {
    sinalizarCadastroOk();
  }
}

// Funções de controle dos periféricos. Evitam repetição de código.
void desligarLeds() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);
}

void sinalizarSucesso() {
  digitalWrite(LED_VERDE, HIGH);
  tone(BUZZER, 2000, 500); // Frequência: 2000Hz | Duração: 500ms
  delay(500);      
  desligarLeds(); 
}

void sinalizarErro() {
  digitalWrite(LED_VERMELHO, HIGH);
  tone(BUZZER, 300, 300); delay(400); // Tom grave e pausado para indicar bloqueio
  tone(BUZZER, 300, 300); delay(400);
  desligarLeds(); 
}

void sinalizarModoCadastro() {
  digitalWrite(LED_VERDE, HIGH);
  digitalWrite(LED_VERMELHO, HIGH);
  tone(BUZZER, 1000, 150); // Beep rápido de notificação
}

void sinalizarCadastroOk() {
  desligarLeds();
  // Toque musical alegre simulando sucesso no cadastro
  tone(BUZZER, 1500, 150); delay(150); 
  tone(BUZZER, 2500, 300); delay(300);
}