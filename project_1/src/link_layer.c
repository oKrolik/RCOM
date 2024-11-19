#include "link_layer.h"
#include "serial_port.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

// Define POSIX compliance para compatibilidade com sistemas POSIX
#define _POSIX_SOURCE 1

// Define valores booleanos para o código
#define FALSE 0
#define TRUE 1

// Define o tamanho máximo da carga útil e da trama de controle (SET ou UA)
#define MAX_DATA_SIZE 1000
#define CONTROL_FRAME_SIZE 5

// Define valores de octetos usados no protocolo de enlace
#define FLAG 0x7E
#define A 0x03
#define A_Rx 0x01
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B
#define ESC 0x7D
#define ESC_FLAG 0x5E
#define ESC_ESC 0x5D
#define BCC 0x5D

// Variáveis globais de configuração de retransmissões e timeout
int MAX_RETRIES;
int TIMEOUT;

// Flags de controle para o alarme
int alarmEnabled = FALSE;
int alarmCount = 0;

extern int fd; // Descritor de arquivo da porta serial

// Códigos de controle e resposta
unsigned char C_I = 0x0;
unsigned char C_II = 0x80;
unsigned char RR = 0xAB;	   // 0xAA para 0, AB para 1
unsigned char REJ = 0x54;	   // Código de rejeição de quadro
unsigned char trans_frame = 0; // Número de sequência da trama

LinkLayerRole role; // Define o papel da conexão (Transmissor ou Receptor)

// Enum para estados da leitura de bytes da trama de controle
typedef enum
{
	WAITING_FOR_FLAG, // Aguardando o início da trama
	READING,		  // Lendo bytes da trama
	COMPLETE		  // Trama recebida completamente
} State;

// Definição do enum para os estados da máquina de estados
typedef enum
{
	START,
	FLAG_RCV, // FLAG recebida
	A_RCV,	  // Campo de endereço recebido
	C_RCV,	  // Campo de controle recebido
	BCC_OK,	  // BCC verificado
	END		  // Fim da leitura da trama
} message_state;

// Handler do alarme, incrementa a contagem e exibe o número de tentativas
void alarmHandler(int signal)
{
	alarmEnabled = FALSE; // Desativa alarme após sinal
	alarmCount++;		  // Incrementa contagem do alarme
	printf("Alarme #%d\n", alarmCount);

	if (alarmCount == MAX_RETRIES)
	{
		printf("Número de tentativas excedido\n");
	}
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
	// Configuração dos parâmetros de retransmissão e timeout
	MAX_RETRIES = connectionParameters.nRetransmissions;
	TIMEOUT = connectionParameters.timeout;

	// Inicializa a porta serial com as configurações fornecidas
	fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);
	if (fd < 0)
	{
		return -1; // Retorna erro se a abertura falhar
	}

	role = connectionParameters.role; // Define o papel da conexão

	// Lógica do Transmissor (tx)
	if (role == 0)
	{
		// Inicializa trama SET e UA esperada
		unsigned char SET[CONTROL_FRAME_SIZE] = {FLAG, A, C_SET, A ^ C_SET, FLAG};
		unsigned char UA_EXPECTED[CONTROL_FRAME_SIZE] = {FLAG, A, C_UA, A ^ C_UA, FLAG};

		unsigned char response[CONTROL_FRAME_SIZE] = {0}; // Armazena resposta do receptor

		State state = WAITING_FOR_FLAG; // Estado inicial da máquina de estados

		int index = 0;
		int done = 0;
		int retries = 0;

		// Configura o handler do alarme
		struct sigaction act = {0};
		act.sa_handler = &alarmHandler;
		// sigemptyset(&act.sa_mask);
		// act.sa_flags = 0;

		if (sigaction(SIGALRM, &act, NULL) == -1)
		{
			perror("sigaction");
			exit(EXIT_FAILURE);
		}
		//(void)signal(SIGALRM, alarmHandler);

		// Loop de envio até confirmação ou limite de tentativas
		while (!done && retries < MAX_RETRIES)
		{
			if (!alarmEnabled)
			{
				printf("Transmissor: Enviar SET. \n");
				writeBytesSerialPort(SET, sizeof(SET)); // Enviar trama SET
				alarm(TIMEOUT);							// Ativa alarme com timeout
				alarmEnabled = TRUE;
				retries++;
				state = WAITING_FOR_FLAG;
				index = 0;
			}

			unsigned char byte;
			int bytesRead = readByteSerialPort(&byte);

			// Verifica timeout ou erro de leitura
			if (bytesRead <= 0)
			{
				if (errno == EINTR)
				{
					printf("Timeout. Reenviar SET\n");
					continue;
				}
				else
				{
					return -1; // Erro de leitura
				}
			}

			// Máquina de estados para processar a resposta UA
			if (bytesRead > 0)
			{
				switch (state)
				{
				case WAITING_FOR_FLAG:
					if (byte == FLAG)
					{
						response[index++] = byte;
						state = READING;
					}
					break;
				case READING:
					response[index++] = byte;
					if (index == CONTROL_FRAME_SIZE && byte == FLAG)
					{
						// Confirma recebimento de UA
						if (memcmp(response, UA_EXPECTED, CONTROL_FRAME_SIZE) == 0)
						{
							printf("Transmissor: Recebido UA\n");
							done = 1; // Conexão estabelecida
							retries = 0;
							alarm(0); // Cancela alarme
						}
						index = 0;
						state = WAITING_FOR_FLAG;
					}
					break;
				default:
					break;
				}
			}
		}

		if (!done)
		{
			return -1; // Se falha em receber UA
		}

		alarm(0); // Desativa alarme
		return fd;
	}
	// Lógica do Receptor (rx)
	else if (role == 1)
	{
		// Inicializa trama UA e a SET esperada
		unsigned char SET_EXPECTED[CONTROL_FRAME_SIZE] = {FLAG, A, C_SET, A ^ C_SET, FLAG};
		unsigned char UA[CONTROL_FRAME_SIZE] = {FLAG, A, C_UA, A ^ C_UA, FLAG};

		unsigned char response[CONTROL_FRAME_SIZE] = {0}; // Armazena resposta do transmissor

		State state = WAITING_FOR_FLAG; // Estado inicial da máquina de estados

		int index = 0;
		int done = 0;

		// Loop de recepção até confirmação do SET ou erro
		while (!done)
		{
			unsigned char byte;
			if (readByteSerialPort(&byte) <= 0)
			{
				continue; // Continua tentando até receber um byte
			}

			// Processa o byte de acordo com o estado da leitura
			switch (state)
			{
			case WAITING_FOR_FLAG:
				if (byte == FLAG)
				{
					response[index++] = byte;
					state = READING;
				}
				break;
			case READING:
				response[index++] = byte;
				if (index == CONTROL_FRAME_SIZE && byte == FLAG)
				{
					// Confirma recebimento de SET
					if (memcmp(response, SET_EXPECTED, CONTROL_FRAME_SIZE) == 0)
					{
						printf("Receptor: Recebido SET, enviar UA.\n");
						writeBytesSerialPort(UA, sizeof(UA)); // Enviar trama UA
						done = 1;							  // Conexão estabelecida
					}
					else
					{
						index = 0; // Reinicia se o pacote estiver incorreto
					}
					state = WAITING_FOR_FLAG;
				}
				break;
			default:
				break;
			}
		}
		return fd;
	}
	return -1; // Retorna erro se papel desconhecido
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{

	unsigned char frame[MAX_DATA_SIZE + 6]; // Define o tamanho da trama (cabeçalho + BCC + dados)
	int packetSize = 0;						// Tamanho do pacote de dados

	frame[0] = FLAG; // Início da trama
	frame[1] = A;	 // Endereço

	// Define o campo de controle (C_I) e alterna entre os frames 0 e 1
	frame[2] = (C_I | (trans_frame == 0 ? 0x00 : 0x80)); // Frame 0 para C_I = 0x00, frame 1 para C_I = 0x80
	frame[3] = frame[1] ^ frame[2];						 // Calcula BCC1 como XOR entre A e C_I

	// Calcula o BCC2 a partir dos dados
	unsigned char bcc2 = 0;
	for (int i = 0; i < bufSize; i++)
	{
		bcc2 ^= buf[i];
	}

	// Preenche a trama com os dados e aplica stuffing
	for (int i = 0; i < bufSize; i++)
	{
		// Aplica stuffing se encontrar FLAG ou ESC
		if (buf[i] == FLAG || buf[i] == ESC)
		{
			frame[4 + packetSize] = ESC;
			frame[4 + packetSize + 1] = buf[i] ^ 0x20; // Realiza XOR com 0x20
			packetSize += 2;
		}
		else
		{
			frame[4 + packetSize] = buf[i]; // Insere o dado normalmente
			packetSize++;
		}
	}

	frame[4 + packetSize] = bcc2;	  // Adiciona BCC2 ao final dos dados
	frame[4 + packetSize + 1] = FLAG; // Adiciona FLAG de fechamento

	int totalSize = 4 + packetSize + 2; // Define o tamanho total da trama (Cabeçalho + dados com stuffing + BCC2 + FLAG final)
	int bytes_written = 0;

	// Configura o handler para o alarme
	struct sigaction act = {0};
	act.sa_handler = &alarmHandler;
	// sigemptyset(&act.sa_mask);
	// act.sa_flags = 0;

	if (sigaction(SIGALRM, &act, NULL) == -1)
	{
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	//(void)signal(SIGALRM, alarmHandler);

	int retryCount = 0;	  // Contador de tentativas de reenvio
	int REJ_received = 0; // Flag para rejeição de trama
	alarmCount = 0;

	// Configura resposta esperada: RR para ACK e REJ para NACK
	RR = (trans_frame == 0) ? 0xAB : 0xAA;
	unsigned char response[5];
	unsigned char S_POS[5] = {FLAG, A, RR, A ^ RR, FLAG};	// Resposta RR (ACK)
	unsigned char S_NEG[5] = {FLAG, A, REJ, A ^ REJ, FLAG}; // Resposta REJ (NACK)

	// Loop de tentativas de envio com timeout e retransmissão
	while (retryCount < MAX_RETRIES && alarmCount < 3)
	{
		bytes_written = writeBytesSerialPort(frame, totalSize); // Envia a trama
		alarm(TIMEOUT);											// Define timeout para aguardar resposta
		alarmEnabled = TRUE;
		REJ_received = 0;

		// Loop para aguardar RR/REJ
		while (alarmEnabled)
		{
			int index = 0;

			// Lê bytes da resposta
			while (index < sizeof(response))
			{
				if (readByteSerialPort(&response[index]) > 0)
				{
					index++;
				}
				// Verifica timeout
				else if (!alarmEnabled)
				{
					break;
				}
			}

			// Verifica se recebeu resposta completa
			if (index == sizeof(response))
			{
				if (memcmp(response, S_POS, sizeof(S_POS)) == 0) // RR recebido
				{
					printf("Recebido RR\n");
					trans_frame = (trans_frame == 0) ? 1 : 0; // Alterna frame
					alarm(0);								  // Cancela o alarme
					alarmEnabled = FALSE;
					alarmCount = 0;
					printf("Enviados %d bytes.\n", bytes_written);
					return bufSize; // Retorna sucesso
				}
				else if (memcmp(response, S_NEG, sizeof(S_NEG)) == 0) // REJ recebido
				{
					alarm(0); // Cancela o alarme
					alarmEnabled = FALSE;
					REJ_received = 1;
					break; // Encerra loop para retransmitir
				}
			}
		}

		// Lógica de retransmissão com base em timeout e REJ
		if (!alarmEnabled)
		{
			if (REJ_received == 1)
			{
				retryCount++;
				printf("REJ recebido. Retransmitir trama. Tentativa %d/%d\n", retryCount, MAX_RETRIES);
			}
			else if (alarmCount < MAX_RETRIES)
			{
				printf("Timeout. Retransmitir trama.\n");
			}
		}
	}

	printf("Máximo de tentativas excedido.\n");
	return -1; // Falha após o máximo de tentativas
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
	unsigned char frame[512] = {0};		   // Buffer para armazenar a trama recebida
	unsigned char byte;					   // Byte individual para leitura da trama
	int frame_pos = 0;					   // Posição no buffer da trama
	int buf_pos = 0;					   // Posição no buffer do pacote de dados
	unsigned char calculated_BCC2 = 0;	   // Valor de BCC2 calculado
	unsigned char received_BCC2;		   // Valor de BCC2 recebido
	int done = 0;						   // Flag para indicar o fim da leitura
	RR = (trans_frame == 0) ? 0xAB : 0xAA; // Define o valor esperado de RR

	// Loop para ler a trama byte a byte e armazenar no buffer
	while (!done)
	{
		// Lê um byte de cada vez
		if (readByteSerialPort(&byte) <= 0)
		{
			printf("Erro ao ler da serial port\n");
			return -1; // Retorna erro se não consegue ler bytes
		}

		frame[frame_pos++] = byte; // Armazena o byte lido na posição corrente

		// Se FLAG é encontrada (indica o fim da trama)
		if (byte == FLAG && frame_pos > 1)
		{
			done = 1; // Define a flag de término do loop
		}
	}

	// Verifica se o BCC1 (XOR entre A e C_I) é válido
	if (frame[3] != (frame[1] ^ frame[2]))
	{
		printf("Erro BCC1.\n");
		return -1; // Retorna erro se BCC1 é inválido
	}

	received_BCC2 = frame[frame_pos - 2]; // Obtém o BCC2 recebido (penúltimo byte da trama)

	// Processa a trama (fazendo destuffing) e calcula BCC2
	for (int i = 4; i < frame_pos - 2; i++)
	{
		// Detecção de escape
		if (frame[i] == ESC)
		{
			i++; // Avança para o byte escapado
			// FLAG escapada
			if (frame[i] == ESC_FLAG)
			{
				packet[buf_pos++] = FLAG; // Copia FLAG para o buffer de dados
				calculated_BCC2 ^= FLAG;  // Atualiza o cálculo do BCC2
			}
			// ESC escapado
			else if (frame[i] == ESC_ESC)
			{
				packet[buf_pos++] = ESC; // Copia ESC para o buffer de dados
				calculated_BCC2 ^= ESC;	 // Atualiza o cálculo do BCC2
			}
		}
		else
		{
			packet[buf_pos++] = frame[i]; // Copia o dado para o buffer de dados
			calculated_BCC2 ^= frame[i];  // Atualiza o cálculo do BCC2
		}
	}

	// Verifica se o BCC2 calculado corresponde ao BCC2 recebido
	if (calculated_BCC2 != received_BCC2)
	{
		printf("Erro BCC2. Calculado: 0x%02X, Recebido: 0x%02X\n", calculated_BCC2, received_BCC2);
		unsigned char S_NEG[5] = {FLAG, A, REJ, A ^ REJ, FLAG}; // Mensagem de NACK

		writeBytesSerialPort(S_NEG, sizeof(S_NEG)); // Envia REJ (NACK)
		printf("Receptor: REJ enviado \n");

		return -1; // Retorna erro se BCC2 é inválido
	}
	else
	{
		unsigned char S_POS[5] = {FLAG, A, RR, A ^ RR, FLAG}; // Mensagem de ACK
		trans_frame = (trans_frame == 0) ? 1 : 0;			  // Alterna número da trama
		writeBytesSerialPort(S_POS, sizeof(S_POS));			  // Envia RR (ACK)

		printf("Receptor: RR enviado \n");
		return buf_pos; // Retorna o tamanho do pacote de dados recebido
	}
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
	// Lógica do Transmissor (tx)
	if (role == 0)
	{
		// Inicializa tramas DISC e UA, e a resposta DISC esperada
		unsigned char DISC[CONTROL_FRAME_SIZE] = {FLAG, A, C_DISC, A ^ C_DISC, FLAG};
		unsigned char UA[CONTROL_FRAME_SIZE] = {FLAG, A_Rx, C_UA, A_Rx ^ C_UA, FLAG};
		unsigned char DISC_EXPECTED[CONTROL_FRAME_SIZE] = {FLAG, A_Rx, C_DISC, A_Rx ^ C_DISC, FLAG};

		unsigned char response[CONTROL_FRAME_SIZE] = {0}; // Armazena a resposta recebida

		State state = WAITING_FOR_FLAG; // Estado inicial da máquina de estados

		int index = 0;
		int done = 0;
		int retries = 0;

		alarmEnabled = FALSE;
		alarmCount = 0;

		// Configura o handler do alarme
		struct sigaction act = {0};
		act.sa_handler = &alarmHandler;
		// sigemptyset(&act.sa_mask);
		////act.sa_flags = 0;

		if (sigaction(SIGALRM, &act, NULL) == -1)
		{
			perror("sigaction");
			exit(EXIT_FAILURE);
		}
		//(void)signal(SIGALRM, alarmHandler);

		// Loop de envio e espera de resposta DISC
		while (!done && retries < MAX_RETRIES)
		{
			if (!alarmEnabled)
			{
				printf("Transmissor: Enviando DISC.\n");
				writeBytesSerialPort(DISC, sizeof(DISC)); // Envia trama DISC
				alarm(TIMEOUT);							  // Ativa alarme com timeout
				alarmEnabled = TRUE;
				retries++;
				state = WAITING_FOR_FLAG;
				index = 0;
			}

			unsigned char byte;
			int bytesRead = readByteSerialPort(&byte);

			// Verifica timeout ou erro de leitura
			if (bytesRead <= 0)
			{
				if (errno == EINTR)
				{
					printf("Timeout. Reenviar DISC \n");
					continue;
				}
				else
				{
					return -1; // Erro de leitura
				}
			}

			// Máquina de estados para processar a resposta DISC
			if (bytesRead > 0)
			{
				switch (state)
				{
				case WAITING_FOR_FLAG:
					if (byte == FLAG)
					{

						response[index++] = byte;
						state = READING;
					}
					break;
				case READING:
					response[index++] = byte;
					if (index == CONTROL_FRAME_SIZE && byte == FLAG)
					{
						// Confirma recebimento de DISC
						if (memcmp(response, DISC_EXPECTED, CONTROL_FRAME_SIZE) == 0)
						{
							printf("Transmissor: Recebido DISC\n");
							done = 1;
						}
						index = 0;
						state = WAITING_FOR_FLAG;
					}
					break;
				default:
					break;
				}
			}
		}
		if (!done)
		{
			return -1; // Se falha em receber DISC
		}

		alarm(0); // Desativa alarme
		alarmEnabled = FALSE;

		// Envia UA para finalizar conexão
		if (!alarmEnabled)
		{
			printf("Transmissor: Enviando UA.\n");
			writeBytesSerialPort(UA, sizeof(UA)); // Enviar trama UA
		}

		return 1;
	}
	// Lógica do Receptor (rx)
	else if (role == 1)
	{
		// Inicializa tramas DISC e as respostas esperadas DISC e UA
		unsigned char DISC[CONTROL_FRAME_SIZE] = {FLAG, A_Rx, C_DISC, A_Rx ^ C_DISC, FLAG};
		unsigned char DISC_EXPECTED[CONTROL_FRAME_SIZE] = {FLAG, A, C_DISC, A ^ C_DISC, FLAG};
		unsigned char UA_EXPECTED[CONTROL_FRAME_SIZE] = {FLAG, A_Rx, C_UA, A_Rx ^ C_UA, FLAG};

		unsigned char response[CONTROL_FRAME_SIZE] = {0};  // Armazena a resposta DISC recebida
		unsigned char response2[CONTROL_FRAME_SIZE] = {0}; // Armazena a resposta UA recebida

		State state = WAITING_FOR_FLAG; // Estado inicial da máquina de estados

		int index = 0;
		int done = 0;

		// Loop para esperar e processar o DISC do transmissor
		while (!done)
		{
			unsigned char byte;
			if (readByteSerialPort(&byte) <= 0)
			{
				continue; // Continua tentando até receber um byte
			}

			// Processa o byte de acordo com o estado da leitura
			switch (state)
			{
			case WAITING_FOR_FLAG:
				if (byte == FLAG)
				{
					response[index++] = byte;
					state = READING;
				}
				break;
			case READING:
				response[index++] = byte;
				if (index == CONTROL_FRAME_SIZE && byte == FLAG)
				{
					// Confirma recebimento de DISC
					if (memcmp(response, DISC_EXPECTED, CONTROL_FRAME_SIZE) == 0)
					{
						printf("Receptor: Recebido DISC\n");
						done = 1;
					}
					index = 0;
					state = WAITING_FOR_FLAG;
				}
				break;
			default:
				break;
			}
		}

		printf("Receptor: Enviando DISC.\n");
		writeBytesSerialPort(DISC, sizeof(DISC)); // Envia DISC em resposta ao DISC do transmissor

		// Reinicia variáveis para esperar o UA do transmissor
		done = 0;
		index = 0;
		state = WAITING_FOR_FLAG;

		// Loop para esperar e processar o UA do transmissor
		while (!done)
		{
			unsigned char byte;
			if (readByteSerialPort(&byte) <= 0)
			{
				continue; // Continua tentando até receber um byte
			}

			// Processa o byte de acordo com o estado da leitura
			switch (state)
			{
			case WAITING_FOR_FLAG:
				if (byte == FLAG)
				{
					response2[index++] = byte;
					state = READING;
				}
				break;
			case READING:
				response2[index++] = byte;
				if (index == CONTROL_FRAME_SIZE && byte == FLAG)
				{
					// Confirma recebimento de UA
					if (memcmp(response2, UA_EXPECTED, CONTROL_FRAME_SIZE) == 0)
					{
						printf("Receptor: Recebido UA\n");
						done = 1; // Conexão estabelecida
					}
					index = 0;
					state = WAITING_FOR_FLAG;
				}
				break;
			default:
				break;
			}
		}
		return 1;
	}
	return -1; // Retorna erro se papel desconhecido
}
