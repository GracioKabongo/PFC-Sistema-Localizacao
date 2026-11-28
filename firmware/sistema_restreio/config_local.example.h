#pragma once
// Copiar para config_local.h (ignorado pelo Git). Nunca publicar dados reais.
// Exemplo inactivo: o sketch compila, mas nao inicia MQTT sem configuracao.
constexpr char GK_APN[] = "";
constexpr char GK_APN_USER[] = "";
constexpr char GK_APN_PASSWORD[] = "";
constexpr char GK_SIM_PIN[] = ""; // Vazio se o SIM nao exige PIN.
constexpr char NUMERO_SMS[] = ""; // Numero autorizado, em formato internacional.
constexpr char GK_MQTT_HOST[] = ""; // Broker acessivel pela rede privada/APN.
constexpr int GK_MQTT_PORT = 1883;
constexpr char GK_MQTT_USER[] = "";
constexpr char GK_MQTT_PASSWORD[] = "";
// Apenas ensaio controlado, em rede privada encaminhada; TCP sem TLS.
// Alterar para true SOMENTE depois de confirmar isolamento e ACL do broker.
constexpr bool GK_MQTT_LAB_PRIVADO = false;
