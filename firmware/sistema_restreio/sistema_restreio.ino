#include <Arduino.h>
#include <esp_system.h>
#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <math.h>
#include "gk_types.h"

// GK-01: filas isolam rede e modem da logica local. Hardware preservado.
QueueHandle_t filaComandos,filaSaida,filaSMS;
Preferences preferencias;
bool nvsOk=false;
ZonaPersistida zonaPersistida={0x474B0001,0,-25.8868018,-25.8871626,32.6450340,32.6454332,""};
String bootId,eventoId;
const char* eventoActual="NORMAL";
unsigned long eventoSeq=0;
String ultimoSOS;
void enfileirarJson(JsonDocument& doc,bool ack);
void registarEvento(const char* tipo);
void aplicarLimites();
bool limitesValidos(double n,double s,double o,double l);
void aplicarComando(const char* json);
void tarefaRede(void*);
//https://restreio-60ka41f0a-wscode.vercel.app/
// ============================================================
// PROTOTIPOS
// ============================================================

void conectarMQTT();
void confirmar(const String& id,const char* comando,const char* estado,const char* erro="");
bool enviarSMS(const String& mensagem);

void inicializarGPS();
void atualizarGPS();

bool estaDentroDaArea(
  double latitude,
  double longitude
);

void atualizarLEDdaArea(
  bool dentroDaArea
);

void publicarTelemetria();

void processarSOSFisico();

void receberComando(
  char* topic,
  byte* payload,
  unsigned int length
);

void atualizarPosicaoTeste();

String criarMensagemSOS();

void IRAM_ATTR interrupcaoSOS();


// ============================================================
// CONFIGURACAO LOCAL
// ============================================================

#if __has_include("config_local.h")
#include "config_local.h"
#else
#include "config_local.example.h"
#endif





// ============================================================
// MQTT
// ============================================================

const char* MQTT_BROKER = GK_MQTT_HOST;

const int MQTT_PORT = GK_MQTT_PORT;

const char* MQTT_TOPIC_TELEMETRIA =
  "rastreamento/telemetria";

const char* MQTT_TOPIC_COMANDO =
  "rastreamento/comando";


// ============================================================
// OBJETO
// ============================================================

const char* OBJETO_ID =
  "OBJ001";

const char* OBJETO_NOME =
  "Garagem 1";


// ============================================================
// GSM
// ============================================================
//
// GPIO16 = RX do ESP32
// GPIO17 = TX do ESP32
//
// TX GSM -> GPIO16
// RX GSM -> GPIO17
//
// ============================================================

#define GSM_RX 16
#define GSM_TX 17

HardwareSerial sim(2);


// ============================================================
// GPS
// ============================================================
//
// GPS assumido:
// NEO-6M
//
// Ligação:
//
// TX GPS -> GPIO18
// RX GPS -> GPIO19
// GND    -> GND
//
// ============================================================

#define GPS_RX 18
#define GPS_TX 19

HardwareSerial gpsSerial(1);

TinyGPSPlus gps;


// ============================================================
// NUMERO PARA SMS
// ============================================================

// NUMERO_SMS definido apenas em config_local.h

// ============================================================
// PINOS
// ============================================================

// Botão físico SOS
#define PINO_BOTAO_SOS 25

// LED de saída da área
#define PINO_LED_ALERTA 23


// ============================================================
// GPRS / MQTT
// ============================================================

// Apenas tarefaRede acede ao modem, ao UART2 e aos clientes de rede.
// MQTT inicia uma ligacao TCP de saida; nenhum servidor no dispositivo.
TinyGsm modem(sim);
TinyGsmClient gprsClient(modem);
PubSubClient mqttClient(gprsClient);


// ============================================================
// INTERVALOS
// ============================================================

const unsigned long INTERVALO_TELEMETRIA =
  2000;

const unsigned long INTERVALO_POSICAO_TESTE =
  10000;


// ============================================================
// CONTROLE DE TEMPO
// ============================================================

unsigned long ultimoEnvio =
  0;

unsigned long ultimaMudancaPosicao =
  0;


// ============================================================
// CONTROLE GPS
// ============================================================

// Indica se o GPS já conseguiu posição válida
bool gpsTemFix =
  false;

// Momento em que o GPS conseguiu fix
unsigned long momentoPrimeiroFixGPS =
  0;

// Última latitude válida
double latitudeGPS =
  0.0;

// Última longitude válida
double longitudeGPS =
  0.0;

// Última altitude
double altitudeGPS =
  0.0;

// Última velocidade GPS
double velocidadeGPS =
  0.0;

// Última direção GPS
double direcaoGPS =
  0.0;

// Precisão aproximada
double precisaoGPS =
  0.0;


// ============================================================
// AREA DELIMITADA
// ============================================================
//
// Estas coordenadas são os limites da área.
//
// ============================================================

// double LIMITE_NORTE =
//   -25.942800;

// double LIMITE_SUL =
//   -25.944500;

// double LIMITE_OESTE =
//   32.542000;

// double LIMITE_LESTE =
//   32.544500;







double LIMITE_NORTE =
  -25.8868018;

double LIMITE_SUL =
  -25.8871626;

double LIMITE_OESTE =
  32.6450340;

double LIMITE_LESTE =
  32.6454332;









// ============================================================
// SOS
// ============================================================

bool sosAtivo =
  false;


// ============================================================
// BOTAO SOS
// ============================================================

volatile bool botaoSOSPressionado =
  false;

volatile unsigned long ultimoInterruptBotao =
  0;

const unsigned long DEBOUNCE_SOS =
  500;


// ============================================================
// POSICOES DE TESTE
// ============================================================
//
// Simulacao operacional desactivada; ver tests/simulator.ts.
//
// Quando o GPS conseguir posição válida,
// estas posições deixam de ser utilizadas.
//
// ============================================================

// GK-05: fixtures de simulacao removidas do firmware operacional.

// ============================================================
// DADOS
// ============================================================

double velocidade =
  5.0;

double direcao =
  90.0;

double bateria =
  87.0;

double precisao =
  3.5;


// ============================================================
// ESTADO ANTERIOR DA AREA
// ============================================================
//
// true  = estava dentro
// false = estava fora
//
// Começa como true porque a primeira posição
// de teste está dentro da área.
//
// ============================================================

bool estadoAnteriorArea =
  true;


// ============================================================
// INTERRUPCAO DO BOTAO SOS
// ============================================================

void IRAM_ATTR interrupcaoSOS() {

  unsigned long agora =
    millis();

  if (
    agora -
    ultimoInterruptBotao >=
    DEBOUNCE_SOS
  ) {

    ultimoInterruptBotao =
      agora;

    botaoSOSPressionado =
      true;

  }

}


// ============================================================
// CONECTAR MQTT SOBRE GPRS (executado apenas pela tarefaRede)
// ============================================================

void conectarMQTT() {
  static unsigned long tentativa = 0;
  static bool tentado = false;
  if (mqttClient.connected()) return;
  if (tentado && millis() - tentativa < 10000UL) return;
  tentado = true;
  tentativa = millis();
  String clientId = String("GK_") + OBJETO_ID + "_" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqttClient.connect(clientId.c_str(), GK_MQTT_USER, GK_MQTT_PASSWORD)) {
    if (!mqttClient.subscribe(MQTT_TOPIC_COMANDO, 1)) {
      mqttClient.disconnect();
      Serial.println("MQTT: falha na subscricao.");
    } else {
      Serial.println("MQTT: ligado via GPRS.");
    }
  } else {
    Serial.printf("MQTT: falha de ligacao (%d).\n", mqttClient.state());
  }
}

// ============================================================
// INICIALIZAR GPS
// ============================================================

void inicializarGPS() {

  Serial.println();

  Serial.println(
    "===================================="
  );

  Serial.println(
    "INICIALIZANDO GPS"
  );

  Serial.println(
    "===================================="
  );

  gpsSerial.begin(
    9600,
    SERIAL_8N1,
    GPS_RX,
    GPS_TX
  );

  delay(500);

  Serial.println(
    "GPS iniciado."
  );

  Serial.print(
    "GPS RX: GPIO "
  );

  Serial.println(
    GPS_RX
  );

  Serial.print(
    "GPS TX: GPIO "
  );

  Serial.println(
    GPS_TX
  );

}


// ============================================================
// ATUALIZAR GPS
// ============================================================
//
// Esta função deve ser chamada frequentemente.
//
// Ela lê continuamente os dados NMEA do GPS.
//
// Quando encontrar uma posição válida,
// gpsTemFix passa para true.
//
// ============================================================

void atualizarGPS() {

  while (
    gpsSerial.available()
  ) {

    char c =
      gpsSerial.read();

    gps.encode(
      c
    );

  }


  // ----------------------------------------------------------
  // VERIFICAR POSIÇÃO
  // ----------------------------------------------------------

  if (
    gps.location.isValid()
  ) {

    // --------------------------------------------------------
    // VERIFICAR SE A POSIÇÃO É RECENTE
    // --------------------------------------------------------

    if (
      gps.location.age() <
      5000
    ) {

      latitudeGPS =
        gps.location.lat();

      longitudeGPS =
        gps.location.lng();


      // ------------------------------------------------------
      // ALTITUDE
      // ------------------------------------------------------

      if (
        gps.altitude.isValid()
      ) {

        altitudeGPS =
          gps.altitude.meters();

      }


      // ------------------------------------------------------
      // VELOCIDADE
      // ------------------------------------------------------

      if (
        gps.speed.isValid()
      ) {

        velocidadeGPS =
          gps.speed.kmph();

        velocidade =
          velocidadeGPS;

      }


      // ------------------------------------------------------
      // DIREÇÃO
      // ------------------------------------------------------

      if (
        gps.course.isValid()
      ) {

        direcaoGPS =
          gps.course.deg();

        direcao =
          direcaoGPS;

      }


      // ------------------------------------------------------
      // PRECISÃO
      // ------------------------------------------------------

      // TinyGPS++ não fornece HDOP como "metros".
      // Aqui usamos HDOP como indicador de precisão.
      //
      // Se disponível:
      //
      // HDOP menor = melhor precisão.
      //
      // ------------------------------------------------------

      if (
        gps.hdop.isValid()
      ) {

        precisaoGPS =
          gps.hdop.hdop();

        precisao =
          precisaoGPS;

      }


      // ------------------------------------------------------
      // PRIMEIRO FIX
      // ------------------------------------------------------

      if (
        !gpsTemFix
      ) {

        gpsTemFix =
          true;

        momentoPrimeiroFixGPS =
          millis();

        Serial.println();

        Serial.println(
          "####################################"
        );

        Serial.println(
          "GPS CONSEGUIU FIX!"
        );

        Serial.println(
          "AGORA SERAO USADAS AS COORDENADAS REAIS."
        );

        Serial.println(
          "####################################"
        );

      }

    }

  }


  // ----------------------------------------------------------
  // GPS PERDEU FIX
  // ----------------------------------------------------------

  if (
    gpsTemFix &&
    gps.location.age() >=
    5000
  ) {

    gpsTemFix =
      false;

    Serial.println();

    Serial.println(
      "GPS perdeu o fix."
    );

    Serial.println(
      "Sem posicao actual. Ultima posicao real preservada; sem simulacao."
    );

  }

}


// ============================================================
// OBTER LATITUDE ATUAL
// ============================================================

double obterLatitudeAtual() { return gpsTemFix ? latitudeGPS : NAN; }


// ============================================================
// OBTER LONGITUDE ATUAL
// ============================================================

double obterLongitudeAtual() { return gpsTemFix ? longitudeGPS : NAN; }


// ============================================================
// VERIFICAR AREA
// ============================================================

bool estaDentroDaArea(
  double latitude,
  double longitude
) {

  bool latitudeOK =
    latitude >=
    LIMITE_SUL &&
    latitude <=
    LIMITE_NORTE;

  bool longitudeOK =
    longitude >=
    LIMITE_OESTE &&
    longitude <=
    LIMITE_LESTE;

  return (
    latitudeOK &&
    longitudeOK
  );

}


// ============================================================
// LED DA AREA
// ============================================================
//
// DENTRO DA AREA -> LED OFF
//
// FORA DA AREA -> LED ON
//
// SAIU:
// LED ON + 1 SMS
//
// RETORNOU:
// LED OFF + 1 SMS
//
// ============================================================

void atualizarLEDdaArea(
  bool dentroDaArea
) {

  // ==========================================================
  // CONTROLE DO LED
  // ==========================================================

  if (
    dentroDaArea
  ) {

    digitalWrite(
      PINO_LED_ALERTA,
      LOW
    );

  }

  else {

    digitalWrite(
      PINO_LED_ALERTA,
      HIGH
    );

  }


  // ==========================================================
  // SÓ ENVIA SMS SE O ESTADO MUDOU
  // ==========================================================

  if (dentroDaArea != estadoAnteriorArea) {
    estadoAnteriorArea=dentroDaArea;
    registarEvento(dentroDaArea?"RETORNO_A_AREA":"SAIDA_DA_AREA");

    Serial.println();

    Serial.println(
      "===================================="
    );


    // ========================================================
    // SAIU DA ÁREA
    // ========================================================

    if (
      !dentroDaArea
    ) {

      Serial.println(
        "LOCALIZACAO: FORA DA AREA"
      );

      Serial.println(
        "LED DE ALERTA: ACESO"
      );

      Serial.println(
        "ENVIANDO SMS: SAIDA DA AREA"
      );


      String mensagem =
        "ALERTA!\n";

      mensagem +=
        "OBJETO: ";

      mensagem +=
        OBJETO_ID;

      mensagem +=
        "\n";

      mensagem +=
        "O objeto SAIU DA AREA DELIMITADA.\n";

      mensagem +=
        "LED DE ALERTA ACESO.\n";


      if (
        gpsTemFix
      ) {

        mensagem +=
          "LAT: ";

        mensagem +=
          String(
            latitudeGPS,
            6
          );

        mensagem +=
          "\n";

        mensagem +=
          "LON: ";

        mensagem +=
          String(
            longitudeGPS,
            6
          );

      }


      enviarSMS(
        mensagem
      );

    }


    // ========================================================
    // RETORNOU PARA A ÁREA
    // ========================================================

    else {

      Serial.println(
        "LOCALIZACAO: DENTRO DA AREA"
      );

      Serial.println(
        "LED DE ALERTA: DESLIGADO"
      );

      Serial.println(
        "ENVIANDO SMS: RETORNO PARA AREA"
      );


      String mensagem =
        "INFORMACAO!\n";

      mensagem +=
        "OBJETO: ";

      mensagem +=
        OBJETO_ID;

      mensagem +=
        "\n";

      mensagem +=
        "O objeto RETORNOU PARA A AREA DELIMITADA.\n";

      mensagem +=
        "LED DE ALERTA DESLIGADO.\n";


      if (
        gpsTemFix
      ) {

        mensagem +=
          "LAT: ";

        mensagem +=
          String(
            latitudeGPS,
            6
          );

        mensagem +=
          "\n";

        mensagem +=
          "LON: ";

        mensagem +=
          String(
            longitudeGPS,
            6
          );

      }


      enviarSMS(
        mensagem
      );

    }


    Serial.println(
      "===================================="
    );


    // ========================================================
    // GUARDAR ESTADO
    // ========================================================

    estadoAnteriorArea =
      dentroDaArea;

  }

}


// ============================================================
// CRIAR MENSAGEM SOS
// ============================================================

String criarMensagemSOS() {
 String texto=String("ALERTA SOS\nID: ")+OBJETO_ID;
 if(!gpsTemFix) return texto+"\nGNSS SEM FIX. Posicao actual indisponivel.";
 return texto+"\nLAT: "+String(latitudeGPS,7)+"\nLON: "+String(longitudeGPS,7)+"\nhttps://maps.google.com/?q="+String(latitudeGPS,7)+","+String(longitudeGPS,7);
}


// ============================================================
// PUBLICAR TELEMETRIA MQTT
// ============================================================

void publicarTelemetria() {
 JsonDocument doc;
 doc["id"]=OBJETO_ID; doc["nome"]=OBJETO_NOME;
 doc["gpsFix"]=gpsTemFix; doc["origem"]=gpsTemFix?"GNSS":"INDISPONIVEL";
 doc["latitude"]=nullptr; doc["longitude"]=nullptr;doc["altitude"]=nullptr;doc["velocidade"]=nullptr;doc["direcao"]=nullptr;doc["precisao"]=nullptr;
 if(gpsTemFix){doc["latitude"]=latitudeGPS;doc["longitude"]=longitudeGPS;
 if(gps.altitude.isValid()&&gps.altitude.age()<5000)doc["altitude"]=altitudeGPS;
 if(gps.speed.isValid()&&gps.speed.age()<5000)doc["velocidade"]=velocidade;
 if(gps.course.isValid()&&gps.course.age()<5000)doc["direcao"]=direcao;
 if(gps.hdop.isValid()&&gps.hdop.age()<5000)doc["precisao"]=precisao;}
 doc["bateria"]=nullptr;doc["bateriaMedida"]=false;
 doc["limiteNorte"]=LIMITE_NORTE;doc["limiteSul"]=LIMITE_SUL;doc["limiteOeste"]=LIMITE_OESTE;doc["limiteLeste"]=LIMITE_LESTE;
 doc["dentroDaArea"]=nullptr;doc["foraDaArea"]=nullptr;
 if(gpsTemFix){bool dentro=estaDentroDaArea(latitudeGPS,longitudeGPS);doc["dentroDaArea"]=dentro;doc["foraDaArea"]=!dentro;}
 doc["localizacao"]=!gpsTemFix?"SEM POSICAO VALIDA":estadoAnteriorArea?"DENTRO DA AREA":"FORA DA AREA";
 doc["ledAlerta"]=digitalRead(PINO_LED_ALERTA)==HIGH;doc["sos"]=sosAtivo?1:0;
 doc["evento"]=eventoActual;doc["eventId"]=eventoId;
 doc["timestamp"]=millis();doc["versaoConfiguracao"]=zonaPersistida.versao;
 enfileirarJson(doc,false);
}


// ============================================================
// PROCESSAR BOTAO FISICO SOS
// ============================================================

void processarSOSFisico() {
 if(!botaoSOSPressionado)return;
 noInterrupts();botaoSOSPressionado=false;interrupts();
 sosAtivo=true;registarEvento("SOS_FISICO");enviarSMS(criarMensagemSOS());publicarTelemetria();sosAtivo=false;
}


// ============================================================
// RECEBER COMANDO MQTT
// ============================================================

void receberComando(char* topic, byte* payload, unsigned int length) {
 if(String(topic)!=MQTT_TOPIC_COMANDO||length>=1024)return;
 MensagemEntrada m={};memcpy(m.json,payload,length);
 if(xQueueSend(filaComandos,&m,0)!=pdTRUE)Serial.println("Fila de comandos cheia; servidor repetira configuracao.");
}


// ============================================================
// ATUALIZAR POSICAO DE TESTE
// ============================================================
//
// IMPORTANTE:
//
// Só funciona enquanto NÃO houver GPS fix.
//
// Esta funcao e deliberadamente inactiva.
//
// ============================================================

void atualizarPosicaoTeste() { /* Desactivado. Simulador externo isolado em tests/. */ }


// ============================================================
// SETUP
// ============================================================

void setup() {
 Serial.begin(115200);
 pinMode(PINO_LED_ALERTA,OUTPUT);digitalWrite(PINO_LED_ALERTA,LOW);
 pinMode(PINO_BOTAO_SOS,INPUT_PULLUP);attachInterrupt(digitalPinToInterrupt(PINO_BOTAO_SOS),interrupcaoSOS,FALLING);
 filaComandos=xQueueCreate(8,sizeof(MensagemEntrada));filaSaida=xQueueCreate(24,sizeof(MensagemSaida));filaSMS=xQueueCreate(8,sizeof(MensagemSMS));
 if(!filaComandos||!filaSaida||!filaSMS){Serial.println("ERRO: memoria insuficiente");while(true)delay(1000);}
 bootId=String((uint32_t)esp_random(),HEX);
 nvsOk=preferencias.begin("gk-zona",false);
 if(nvsOk&&preferencias.getBytesLength("zona")==sizeof(ZonaPersistida)){
 ZonaPersistida salva={};preferencias.getBytes("zona",&salva,sizeof(salva));
 if(salva.magic==0x474B0001&&limitesValidos(salva.norte,salva.sul,salva.oeste,salva.leste))zonaPersistida=salva;
 }
 aplicarLimites();inicializarGPS();
 sim.begin(9600,SERIAL_8N1,GSM_RX,GSM_TX);
 if (xTaskCreate(tarefaRede,"gk-modem",12288,nullptr,1,nullptr) != pdPASS) {
   Serial.println("ERRO: tarefa de comunicacao indisponivel; monitoria local activa.");
 }
 Serial.println("GK: seguranca local activa; GNSS real, sem fallback simulado.");
}


// ============================================================
// LOOP
// ============================================================

void loop() {
 atualizarGPS();processarSOSFisico();
 MensagemEntrada m;if(xQueueReceive(filaComandos,&m,0)==pdTRUE)aplicarComando(m.json);
 if(gpsTemFix)atualizarLEDdaArea(estaDentroDaArea(latitudeGPS,longitudeGPS));
 if(millis()-ultimoEnvio>=INTERVALO_TELEMETRIA){ultimoEnvio=millis();publicarTelemetria();}
 delay(2);
}
// GK-02: persistencia num unico blob NVS, validacao e confirmacoes correlacionadas.
bool limitesValidos(double n,double s,double o,double l){return isfinite(n)&&isfinite(s)&&isfinite(o)&&isfinite(l)&&n<=90&&s>=-90&&n>s&&o>=-180&&l<=180&&l>o;}
void aplicarLimites(){LIMITE_NORTE=zonaPersistida.norte;LIMITE_SUL=zonaPersistida.sul;LIMITE_OESTE=zonaPersistida.oeste;LIMITE_LESTE=zonaPersistida.leste;}
void enfileirarJson(JsonDocument& doc,bool ack){MensagemSaida m={};m.criada=millis();m.ack=ack;serializeJson(doc,m.json,sizeof(m.json));if(xQueueSend(filaSaida,&m,0)!=pdTRUE)Serial.println("Fila MQTT cheia: registo descartado; confirmar no teste offline.");}
void registarEvento(const char* tipo){eventoActual=tipo;eventoId=bootId+"-"+String(++eventoSeq);publicarTelemetria();}
void confirmar(const String& id,const char* comando,const char* estado,const char* erro){
 JsonDocument a;a["id"]=OBJETO_ID;a["commandId"]=id;a["comando"]=comando;a["estado"]=estado;a["erro"]=erro;a["versaoConfiguracao"]=zonaPersistida.versao;
 if(String(comando)=="configurar_geofencing"){auto z=a["zona"].to<JsonObject>();z["tipo"]="rectangle";z["norte"]=LIMITE_NORTE;z["sul"]=LIMITE_SUL;z["oeste"]=LIMITE_OESTE;z["leste"]=LIMITE_LESTE;}
 enfileirarJson(a,true);
}
void aplicarComando(const char* json){
 JsonDocument d;if(deserializeJson(d,json))return;
 if(String(d["id"]|"")!=OBJETO_ID)return;
 String id=d["commandId"]|"";bool isZone=String(d["comando"]|"")=="configurar_geofencing";
 if(isZone){
 if(id.length()!=36){return;}
 confirmar(id,"configurar_geofencing","recebido");auto z=d["zona"];
 if(!d["versaoConfiguracao"].is<unsigned int>()||String(z["tipo"]|"")!="rectangle"||!z["norte"].is<double>()||!z["sul"].is<double>()||!z["oeste"].is<double>()||!z["leste"].is<double>()){confirmar(id,"configurar_geofencing","rejeitado","Formato invalido");return;}
 double n=z["norte"],s=z["sul"],o=z["oeste"],l=z["leste"];uint32_t v=d["versaoConfiguracao"];
 if(!limitesValidos(n,s,o,l)||v==0||v>2147483647){confirmar(id,"configurar_geofencing","rejeitado","Limites ou versao invalidos");return;}
 if(v==zonaPersistida.versao&&id==zonaPersistida.commandId&&n==LIMITE_NORTE&&s==LIMITE_SUL&&o==LIMITE_OESTE&&l==LIMITE_LESTE){confirmar(id,"configurar_geofencing","aplicado");return;}
 if(v<=zonaPersistida.versao){confirmar(id,"configurar_geofencing","rejeitado","Versao antiga ou conflito");return;}
 ZonaPersistida nova={0x474B0001,v,n,s,o,l,""};id.toCharArray(nova.commandId,sizeof(nova.commandId));
 if(!nvsOk||preferencias.putBytes("zona",&nova,sizeof(nova))!=sizeof(nova)){confirmar(id,"configurar_geofencing","rejeitado","Falha NVS");return;}
 zonaPersistida=nova;aplicarLimites();if(gpsTemFix)atualizarLEDdaArea(estaDentroDaArea(latitudeGPS,longitudeGPS));confirmar(id,"configurar_geofencing","aplicado");publicarTelemetria();return;
 }
 if((d["sos"].is<int>()&&d["sos"].as<int>()==1)||(d["sos"].is<bool>()&&d["sos"].as<bool>())){
 if(id.length()&&id==ultimoSOS){confirmar(id,"sos","executado");return;}
 if(id.length())confirmar(id,"sos","recebido");
 sosAtivo=true;registarEvento("SOS_REMOTO");bool queued=enviarSMS(criarMensagemSOS());publicarTelemetria();sosAtivo=false;
 if(queued){ultimoSOS=id;if(id.length())confirmar(id,"sos","executado","Rotina executada; SMS enfileirado, entrega nao confirmada");}
 else if(id.length())confirmar(id,"sos","rejeitado","Fila SMS cheia");
 }
}
// GK-03/GK-04: uma unica tarefa possui o SIM800L e UART2.
// As esperas de rede nao bloqueiam o loop de GNSS, geofencing ou SOS fisico.
// SMS tem prioridade entre operacoes, mas nao interrompe comandos AT em curso.
void tarefaRede(void*) {
  bool mqttPermitido = GK_MQTT_LAB_PRIVADO && GK_APN[0] &&
      MQTT_BROKER[0] && GK_MQTT_USER[0] && GK_MQTT_PASSWORD[0] &&
      MQTT_PORT > 0 && MQTT_PORT <= 65535;
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(receberComando);
  if (!mqttClient.setBufferSize(2048)) mqttPermitido = false;
  mqttClient.setSocketTimeout(10);
  mqttClient.setKeepAlive(60);
  if (!mqttPermitido) Serial.println("MQTT desactivado: configurar rede privada de ensaio, APN e credenciais.");
  MensagemSaida m = {};
  bool pendente = false;
  bool inicializado = false, rede = false, dados = false;
  bool consultado = false, tentouDados = false;
  unsigned long ultimaConsulta = 0, ultimaTentativaDados = 0;

  for (;;) {
    if (!inicializado) {
      inicializado = modem.init(GK_SIM_PIN[0] ? GK_SIM_PIN : nullptr);
      if (!inicializado) {
        Serial.println("SIM800L: inicializacao falhou.");
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
      consultado = false;
      Serial.println("SIM800L: inicializado.");
    }

    if (!consultado || millis() - ultimaConsulta >= 5000UL) {
      consultado = true;
      ultimaConsulta = millis();
      rede = modem.isNetworkConnected();
      if (dados && (!rede || !modem.isGprsConnected())) {
        dados = false;
        gprsClient.stop();
        Serial.println("GPRS: ligacao perdida.");
      }
      if (!rede && !modem.testAT(1000L)) {
        inicializado = false;
        dados = false;
        continue;
      }
    }

    // Nao retirar da fila enquanto a rede GSM nao estiver registada.
    MensagemSMS sms = {};
    if (rede && xQueueReceive(filaSMS, &sms, 0) == pdTRUE) {
      bool ok = NUMERO_SMS[0] && modem.sendSMS(String(NUMERO_SMS), String(sms.texto));
      Serial.println(ok ? "SMS: ACEITE PELO MODEM; ENTREGA NAO CONFIRMADA"
                        : "SMS: FALHA/SEM CONFIRMACAO");
      // Nao repetir automaticamente um envio ambiguo (risco de duplicacao).
      consultado = false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (!rede || !mqttPermitido) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!dados && (!tentouDados || millis() - ultimaTentativaDados >= 15000UL)) {
      tentouDados = true;
      Serial.println("GPRS: tentativa de ligacao.");
      dados = modem.gprsConnect(GK_APN, GK_APN_USER, GK_APN_PASSWORD);
      ultimaTentativaDados = millis();
      consultado = false;
      Serial.println(dados ? "GPRS: ligado." : "GPRS: falha.");
      // Voltar ao inicio para dar prioridade a SMS entretanto enfileirados.
      continue;
    }

    if (dados) {
      conectarMQTT();
      if (mqttClient.connected()) {
        mqttClient.loop();
        if (!pendente) pendente = xQueueReceive(filaSaida, &m, 0) == pdTRUE;
        if (pendente && !m.ack && millis() - m.criada > 5000) {
          JsonDocument antiga;
          if (!deserializeJson(antiga, m.json)) {
            antiga["gpsFix"] = false;
            antiga["origem"] = "INDISPONIVEL";
            antiga["latitude"] = nullptr;
            antiga["longitude"] = nullptr;
            antiga["dentroDaArea"] = nullptr;
            antiga["foraDaArea"] = nullptr;
            antiga["historicoOffline"] = true;
            serializeJson(antiga, m.json, sizeof(m.json));
          }
        }
        // PubSubClient publica em QoS 0: sucesso local nao comprova entrega.
        if (pendente && mqttClient.publish(m.ack ? "rastreamento/confirmacao" :
                                          MQTT_TOPIC_TELEMETRIA, m.json)) pendente = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool enviarSMS(const String& mensagem) {
  MensagemSMS m = {};
  mensagem.toCharArray(m.texto, sizeof(m.texto));
  if (xQueueSend(filaSMS, &m, 0) == pdTRUE) return true;
  Serial.println("Fila SMS cheia: alerta nao enfileirado.");
  return false;
}
