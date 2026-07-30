# UDP Remote Logger Receiver

Este diretório contém o script utilitário para receber e gravar os logs transmitidos por rede (UDP) a partir dos dispositivos da fazenda inteligente (como o Water Tank e o Hub).

## Como funciona
Os dispositivos, ao estarem conectados na rede Wi-Fi, redirecionam a saída padrão do `ESP_LOGx` para pacotes UDP na porta `4444`. 
Este script escuta essa porta, adiciona um timestamp com milissegundos e o IP do dispositivo de origem, imprimindo na tela e salvando as saídas em arquivo de texto.

## Requisitos
- Apenas Python 3 instalado (usa módulos da biblioteca padrão do Python).

## Como Executar

1. Abra o terminal na pasta deste logger:
   ```bash
   cd logger
   ```

2. Execute o script receiver:
   ```bash
   python3 udp_logger.py
   ```

Os logs serão exibidos no console em tempo real e armazenados em `field_test_logs.txt`.
