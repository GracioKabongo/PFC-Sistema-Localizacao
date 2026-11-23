#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
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
void tarefaGSM(void*);
//https://restreio-60ka41f0a-wscode.vercel.app/
// ============================================================
// PROTOTIPOS
// ============================================================

void conectarWiFi();
void conectarMQTT();

void inicializacaoGSM();
void atualizarSerialGSM(unsigned long tempo);
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
// WIFI
// ============================================================

#include "config_local.h"





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
// WIFI / MQTT
// ============================================================

#if GK_MQTT_TLS
WiFiClientSecure espClient;
#else
WiFiClient espClient;
#endif

PubSubClient mqttClient(
  espClient
);


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
// CONECTAR WIFI
// ============================================================

void conectarWiFi() {
 static unsigned long tentativa=0;
 if(WiFi.status()==WL_CONNECTED) return;
 if(tentativa && millis()-tentativa<15000) return;
 tentativa=millis(); WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID,WIFI_SENHA);
}


// ============================================================
// CONECTAR MQTT
// ============================================================

void conectarMQTT() {
 static unsigned long tentativa=0;
 if(mqttClient.connected() || WiFi.status()!=WL_CONNECTED) return;
 if(tentativa && millis()-tentativa<5000) return;
 tentativa=millis();
 String clientId=String("GK_")+OBJETO_ID+"_"+String((uint32_t)ESP.getEfuseMac(),HEX);
 if(mqttClient.connect(clientId.c_str(),GK_MQTT_USER,GK_MQTT_PASSWORD)) mqttClient.subscribe(MQTT_TOPIC_COMANDO,1);
}


// ============================================================
// GSM - ATUALIZAR SERIAL
// ============================================================

void atualizarSerialGSM(
  unsigned long tempo
) {

  unsigned long inicio =
    millis();

  while (
    millis() -
    inicio <
    tempo
  ) {

    while (
      sim.available()
    ) {

      Serial.write(
        sim.read()
      );

    }

    while (
      Serial.available()
    ) {

      sim.write(
        Serial.read()
      );

    }

    delay(5);

  }

}


// ============================================================
// INICIALIZAR GSM
// ============================================================

void inicializacaoGSM() {

  Serial.println();

  Serial.println(
    "===================================="
  );

  Serial.println(
    "INICIALIZANDO GSM"
  );

  Serial.println(
    "===================================="
  );

  delay(300);


  sim.println(
    "AT"
  );

  atualizarSerialGSM(
    300
  );


  sim.println(
    "AT+CMGF=1"
  );

  atualizarSerialGSM(
    300
  );


  sim.println(
    "AT+CNMI=1,2,0,0,0"
  );

  atualizarSerialGSM(
    300
  );

  Serial.println(
    "GSM pronto."
  );

}


// ============================================================
// ENVIAR SMS
// ============================================================

bool enviarSMSModem(
  const String& mensagem
) {

  Serial.println();

  Serial.println(
    "===================================="
  );

  Serial.println(
    "ENVIO DE SMS"
  );

  Serial.println(
    "===================================="
  );


  sim.println(
    "AT"
  );

  atualizarSerialGSM(
    200
  );


  sim.println(
    "AT+CMGF=1"
  );

  atualizarSerialGSM(
    200
  );


  sim.print(
    "AT+CMGS=\""
  );

  sim.print(
    NUMERO_SMS
  );

  sim.println(
    "\""
  );


  unsigned long inicio =
    millis();

  bool pronto =
    false;

  while (
    millis() -
    inicio <
    2500
  ) {

    while (
      sim.available()
    ) {

      char c =
        sim.read();

      Serial.write(
        c
      );

      if (
        c ==
        '>'
      ) {

        pronto =
          true;

        break;

      }

    }

    if (
      pronto
    ) {

      break;

    }

    delay(10);

  }


  if (
    !pronto
  ) {

    Serial.println();

    Serial.println(
      "GSM nao respondeu ao AT+CMGS."
    );

    return false;

  }


  sim.print(
    mensagem
  );


  sim.write(
    26
  );


  inicio =
    millis();

  String resposta;


  while (
    millis() -
    inicio <
    5000
  ) {

    while (
      sim.available()
    ) {

      char c =
        sim.read();

      Serial.write(
        c
      );

      resposta +=
        c;

    }


    if (
      resposta.indexOf("+CMGS:") >= 0 && resposta.indexOf("OK") >= 0
    ) {

      Serial.println();

      Serial.println(
        "Modem aceitou SMS; entrega ao destinatario nao confirmada."
      );

      return true;

    }


    if (
      resposta.indexOf(
        "ERROR"
      ) >= 0
    ) {

      Serial.println();

      Serial.println(
        "Erro no envio do SMS."
      );

      return false;

    }

    delay(10);

  }


  Serial.println();

  Serial.println(
    "Timeout aguardando SMS."
  );

  return false;

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
 xTaskCreate(tarefaGSM,"gk-gsm",4096,nullptr,1,nullptr);
 xTaskCreate(tarefaRede,"gk-rede",8192,nullptr,1,nullptr);
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
void confirmar(const String& id,const char* comando,const char* estado,const char* erro=""){
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
// GK-03: tarefa MQTT pode aguardar rede sem suspender GNSS, botao ou LED.
void tarefaRede(void*){
 #if GK_MQTT_TLS
 espClient.setCACert(GK_MQTT_CA);
 configTime(0,0,"pool.ntp.org");
 #endif
 mqttClient.setServer(MQTT_BROKER,MQTT_PORT);mqttClient.setCallback(receberComando);mqttClient.setBufferSize(2048);mqttClient.setSocketTimeout(2);
 MensagemSaida m={};bool pendente=false;
 for(;;){conectarWiFi();conectarMQTT();if(mqttClient.connected()){
 mqttClient.loop();if(!pendente)pendente=xQueueReceive(filaSaida,&m,0)==pdTRUE;
 if(pendente&&!m.ack&&millis()-m.criada>5000){
 JsonDocument antiga;if(!deserializeJson(antiga,m.json)){
 antiga["gpsFix"]=false;antiga["origem"]="INDISPONIVEL";antiga["latitude"]=nullptr;antiga["longitude"]=nullptr;
 antiga["dentroDaArea"]=nullptr;antiga["foraDaArea"]=nullptr;antiga["historicoOffline"]=true;
 serializeJson(antiga,m.json,sizeof(m.json));}
 }
 if(pendente&&mqttClient.publish(m.ack?"rastreamento/confirmacao":MQTT_TOPIC_TELEMETRIA,m.json))pendente=false;
 }vTaskDelay(pdMS_TO_TICKS(10));}
}
// GK-04: SIM800L continua a enviar SMS; aceitar na fila nao prova entrega.
bool enviarSMS(const String& mensagem){MensagemSMS m={};mensagem.toCharArray(m.texto,sizeof(m.texto));return xQueueSend(filaSMS,&m,0)==pdTRUE;}
void tarefaGSM(void*){inicializacaoGSM();MensagemSMS m;for(;;){if(xQueueReceive(filaSMS,&m,portMAX_DELAY)==pdTRUE){bool ok=enviarSMSModem(String(m.texto));Serial.println(ok?"SMS: ACEITE PELO MODEM":"SMS: FALHA/SEM CONFIRMACAO");}}}
