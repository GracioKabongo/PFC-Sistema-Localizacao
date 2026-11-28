# PFC — Sistema de Notificação e Localização de Pessoas Desaparecidas

## Firmware móvel (GPRS) — versão em validação

O ESP32 adquire coordenadas através do NEO-6M, avalia localmente o geofencing e processa o botão SOS. O SIM800L estabelece uma ligação TCP de saída via GPRS (rede 2G) para o broker MQTT e envia notificações SMS através do GSM. Não é necessário atribuir um IP público ao dispositivo nem abrir portas de entrada no mesmo.

A plataforma web comunica com o broker através dos tópicos existentes: `rastreamento/telemetria`, `rastreamento/comando` e `rastreamento/confirmacao`. O formato das mensagens foi mantido; a plataforma não integra este repositório.

## Hardware preservado

| Componente | Ligações ao ESP32 |
|---|---|
| NEO-6M | UART1: RX 18 / TX 19, 9600 baud |
| SIM800L | UART2: RX 16 / TX 17, 9600 baud |
| Botão SOS | GPIO25, INPUT_PULLUP |
| LED de alerta | GPIO23 |

Confirmar alimentação adequada ao SIM800L (incluindo picos de corrente), terra comum e níveis eléctricos compatíveis antes de carregar.

## Instalação

1. Abrir [firmware/sistema_restreio/sistema_restreio.ino](firmware/sistema_restreio/sistema_restreio.ino) no Arduino IDE, mantendo os cabeçalhos na mesma pasta.
2. Instalar o núcleo ESP32 e as bibliotecas TinyGSM (0.12.0), PubSubClient (2.8), ArduinoJson (7.x) e TinyGPSPlus.
3. Copiar `config_local.example.h` para `config_local.h`. Preencher o APN efectivo da operadora, PIN (se necessário), número SMS autorizado, endereço/porta do broker e credenciais. Nunca versionar este ficheiro.
4. Confirmar cobertura 2G/GPRS e encaminhamento entre a rede móvel e o broker. Uma rede privada exige APN privado, VPN/encaminhamento no lado da infra-estrutura ou solução equivalente. Um endereço privado numa LAN não fica acessível por GPRS comum automaticamente.
5. Configurar o broker com autenticação e ACL limitadas aos tópicos necessários. Esta versão usa MQTT/TCP sem TLS: só se destina a ensaios controlados numa rede privada encaminhada. Não usar broker público partilhado nem dados de pessoas vulneráveis. A ausência de IP público próprio não garante segurança. Para produção é necessário transporte com autenticação do servidor e cifragem verificadas.
6. Depois de confirmar as condições anteriores, activar `GK_MQTT_LAB_PRIVADO` na configuração local. Sem configuração, MQTT permanece desactivado.
7. Verificar a compilação para a placa física, carregar e abrir o monitor série a 115200 baud. Rever a zona segura inicial do sketch ou a configuração persistida antes de testar com o destinatário SMS informado.

## Funcionamento e limites

Uma única tarefa FreeRTOS é proprietária do modem e do UART2: inicialização, registo GSM, GPRS, cliente MQTT e envio de SMS. Os alertas SMS têm prioridade entre operações AT, mas um comando em curso (incluindo GPRS e TCP) pode atrasar o próximo envio. O loop local continua a adquirir GNSS, classificar a posição, accionar o LED e processar o botão. Sem fix válido, não há novas classificações de geofencing. Sem GPRS, a monitoria remota e os comandos MQTT ficam indisponíveis; SMS continua a depender do registo GSM. Filas RAM são limitadas e perdem conteúdo após reinício; a zona segura é persistida em NVS.

A aceitação de SMS pelo modem não comprova recepção no telefone. Publicação MQTT em QoS 0 não comprova recepção no servidor. O campo timestamp é tempo de funcionamento em milissegundos, não relógio sincronizado. A validação física está pendente; resultados de ensaios de versões anteriores não devem ser atribuídos a esta versão.

Consultar [protocolo de validação](docs/VALIDACAO_GPRS.md). Referência da biblioteca: https://github.com/vshymanskyy/TinyGSM.
