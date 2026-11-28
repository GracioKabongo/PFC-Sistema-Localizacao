#pragma once
#include <Arduino.h>
struct ZonaPersistida { uint32_t magic; uint32_t versao; double norte,sul,oeste,leste; char commandId[37]; };
struct MensagemEntrada { char json[1024]; };
struct MensagemSaida { unsigned long criada; bool ack; char json[1536]; };
struct MensagemSMS { char texto[512]; };
