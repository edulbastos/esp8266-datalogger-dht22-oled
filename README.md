# ESP8266 Datalogger com DHT22 e Display OLED SSD1306

Sistema completo de aquisição e logging de dados de temperatura e umidade usando ESP8266, sensor DHT22 e display OLED SSD1306, com conectividade WiFi, sincronização NTP, publicação MQTT e servidor HTTP.

## Características

- **Aquisição de Dados**: Leitura periódica de temperatura e umidade com sensor DHT22
- **Display OLED**: Visualização em tempo real dos dados no display SSD1306 128x64 (I2C)
- **Conectividade WiFi**: Conexão automática e reconexão inteligente
- **Sincronização de Tempo**: NTP sincronizado com servidores brasileiros
- **Publicação MQTT**: Envio de dados para broker MQTT com batching e retry
- **Servidor HTTP**: Interface web para visualização e configuração
- **Armazenamento SPIFFS**: Backup local de medições quando offline
- **Arquitetura Modular**: Código refatorado com separação clara de responsabilidades
- **FreeRTOS**: Sistema multitarefa com prioridades otimizadas

## Componentes de Hardware

- **Microcontrolador**: ESP8266 (ESP-12E/NodeMCU)
- **Sensor**: DHT22 (temperatura e umidade)
- **Display**: OLED SSD1306 128x64 (I2C)
- **Conexões I2C**:
  - SDA: GPIO12
  - SCL: GPIO14
  - Endereço OLED: 0x3C
- **Sensor DHT22**: GPIO4

## Estrutura do Projeto

```
esp8266_datalogger_dth22_ssd1306/
├── main/
│   ├── main.c              # Ponto de entrada da aplicação
│   ├── config.h            # Configurações gerais do sistema
│   ├── types.h             # Definições de tipos de dados
│   ├── globals.h/c         # Variáveis globais compartilhadas
│   ├── wifi_manager.h/c    # Gerenciamento WiFi
│   ├── ntp_manager.h/c     # Sincronização NTP
│   ├── mqtt_manager.h/c    # Cliente MQTT
│   ├── http_server.h/c     # Servidor HTTP
│   ├── measurement.h/c     # Aquisição de dados DHT22
│   ├── oled_display.h/c    # Controle do display OLED
│   ├── spiffs_manager.h/c  # Gerenciamento do sistema de arquivos
│   ├── dns_manager.h/c     # Cache DNS para broker MQTT
│   ├── system_status.h/c   # Monitoramento do sistema
│   └── time_cache.h/c      # Cache de timestamp NTP
├── components/
│   └── ssd1306/            # Driver do display OLED
├── CMakeLists.txt
├── partitions.csv
└── sdkconfig
```

## Configuração

### Requisitos de Software

- ESP8266_RTOS_SDK
- CMake 3.5+
- Toolchain Xtensa

### Configuração WiFi e MQTT

⚠️ **IMPORTANTE - Segurança**: Este projeto usa o sistema de configuração do ESP-IDF que armazena credenciais no arquivo `sdkconfig`. Este arquivo **NÃO deve ser commitado** no Git pois contém senhas e informações sensíveis.

#### Método 1: Usando menuconfig (Recomendado)

```bash
idf.py menuconfig
```

Navegue até "Component config" → "Project Configuration" e configure:

- **WiFi SSID**: Nome da rede WiFi
- **WiFi Password**: Senha da rede
- **MQTT Broker**: Endereço do broker MQTT (ex: mqtt://broker.hivemq.com:1883)
- **MQTT Username**: Usuário MQTT
- **MQTT Password**: Senha MQTT
- **MQTT Topic Data**: Tópico para publicação de dados (ex: sensors/temperature/data)
- **MQTT Topic Status**: Tópico para status do sistema (ex: sensors/temperature/status)
- **Sensor ID**: Identificador único do sensor (ex: ESP8266-001)
- **Measurement Interval**: Intervalo entre medições em ms (ex: 30000 = 30s)

#### Método 2: Usando arquivo de configuração local

1. Copie o arquivo de exemplo:
```bash
cp sdkconfig.defaults sdkconfig.defaults.local
```

2. Edite `sdkconfig.defaults.local` com suas credenciais (este arquivo é gitignored)

3. Execute o build normalmente:
```bash
idf.py build
```

#### ⚠️ Checklist de Segurança antes do Git Push

Antes de fazer push para o GitHub, **sempre verifique**:

```bash
# Verificar se sdkconfig não está sendo rastreado
git status | grep sdkconfig

# Se aparecer, remova do staging
git reset HEAD sdkconfig sdkconfig.old

# Verifique o .gitignore
cat .gitignore | grep sdkconfig
```

### Compilação e Flash

```bash
# Configurar
idf.py menuconfig

# Compilar
idf.py build

# Fazer upload
idf.py -p /dev/ttyUSB0 flash

# Monitorar
idf.py -p /dev/ttyUSB0 monitor
```

## Funcionalidades Principais

### Sistema de Tasks FreeRTOS

O sistema opera com múltiplas tasks com prioridades otimizadas:

| Task | Prioridade | Função |
|------|-----------|--------|
| oled_display | 7 | Atualização do display OLED |
| wifi_monitor | 6 | Monitoramento de conexão WiFi |
| ntp_sync | 5 | Sincronização NTP |
| mqtt_monitor | 4 | Monitoramento MQTT |
| measurement | 3 | Aquisição de dados DHT22 |
| http_server | 2 | Servidor HTTP |
| system_status | 1 | Status do sistema |

### Publicação MQTT

- **Batching**: Agrupamento de mensagens para otimizar transmissão
- **Retry**: Reenvio automático em caso de falha
- **Backlog**: Armazenamento em SPIFFS quando offline
- **Keep-alive**: Heartbeat periódico para manter conexão

### Display OLED

Exibe em tempo real:
- Temperatura e umidade atuais
- Status da conexão WiFi
- Status da conexão MQTT
- Timestamp da última medição

### Sistema de Backup SPIFFS

- Armazenamento ring buffer de até 20 medições
- Sincronização automática quando reconectar ao MQTT
- Persistência de dados durante quedas de energia

## Formato dos Dados MQTT

```json
{
  "sensor_id": "ESP8266-001",
  "timestamp": 1696348800,
  "temperature": 25.3,
  "humidity": 62.5
}
```

## Monitoramento e Debug

O sistema fornece logs detalhados via UART:

```bash
idf.py monitor
```

Tags de log disponíveis:
- `MAIN` - Inicialização principal
- `WIFI` - Eventos WiFi
- `NTP` - Sincronização de tempo
- `MQTT` - Publicação e status MQTT
- `MEASUREMENT` - Leituras do sensor
- `OLED` - Atualizações do display
- `SPIFFS` - Operações do sistema de arquivos
- `HTTP` - Requisições HTTP

## API HTTP

O servidor HTTP expõe os seguintes endpoints:

- `GET /` - Página principal com status do sistema
- `GET /data` - Últimas medições (JSON)
- `GET /status` - Status completo do sistema (JSON)

## Resolução de Problemas

### WiFi não conecta
- Verificar SSID e senha no menuconfig
- Verificar sinal WiFi e compatibilidade (2.4GHz apenas)

### MQTT não publica
- Verificar conectividade com broker
- Validar credenciais MQTT
- Checar tópicos configurados

### Display não funciona
- Verificar conexões I2C (SDA=GPIO12, SCL=GPIO14)
- Confirmar endereço I2C do display (padrão: 0x3C)

### DHT22 retorna erro
- Verificar conexão no GPIO4
- Aguardar 2 segundos após power-on
- Checar alimentação do sensor (3.3V-5V)

## Licença

Este projeto está licenciado sob os termos da licença MIT.

## Autor

elbastos(at)gmail.com

Desenvolvido para aplicações IoT de monitoramento ambiental.

## Versão

**Firmware Version**: 1.0 
