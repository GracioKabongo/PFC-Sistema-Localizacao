# Protocolo de validação da versão GPRS

**Estado: ensaios a realizar.** Não há resultados experimentais certificados por este ficheiro.

Identificar commit, placa, versão do núcleo/bibliotecas, firmware SIM800L, operadora, APN (sem credenciais), condições de rede e local/data. Usar coordenadas de teste não sensíveis. Guardar registos de emissão, recepção e falha, sem números pessoais ou credenciais no repositório público.

| ID | Ensaio | Evidência esperada |
|---|---|---|
| G01 | Compilar e carregar | Saída do compilador e identificação do binário |
| G02 | Registo GSM e sessão GPRS | Registo série do dispositivo e sessão no broker privado |
| G03 | Telemetria: 20 tentativas identificadas | Registo do emissor e receptor, identificadores correlacionados; taxa de recepção |
| G04 | Comandos: configuração e SOS remoto | Publicação, processamento, confirmação e estado persistido |
| G05 | Interromper GPRS mantendo GSM, recuperar | GNSS/geofencing/LED e SOS físico mantidos; tentativa SMS e reconexão MQTT |
| G06 | Interromper GSM | Sem comunicação externa; processamento local; filas e perdas documentadas |
| G07 | Enviar SMS durante tráfego MQTT | Contagem de tentativas, aceitação pelo modem e recepção no telefone, por evento |
| G08 | Testar saídas e retornos de zona | Estado real, classificação, tentativa SMS e recepção |
| G09 | Medir latência | Relógios sincronizados ou instrumento comum, instantes definidos, n, média, mínimo, máximo e dispersão |
| G10 | Autonomia | Três ciclos na configuração GPRS final, incluindo condições de carga e tráfego |
| G11 | Acesso ao broker | Verificar roteamento, autenticação, ACL e ausência de servidor/portas de entrada no dispositivo |
| G12 | Reinício e fila cheia | Persistência da zona, falhas explícitas e comportamento de recuperação |

Não reutilizar resultados de transporte anterior como medições GPRS. Registar também atrasos durante operações bloqueantes do modem e distinguir processamento de SOS de entrega SMS. A latência de envio e recepção não pode ser calculada directamente a partir de millis() em dispositivos diferentes.
