#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/*
 * connectToServer
 * ----------------
 * Essa função estabelece uma conexão com um servidor especificado.
 *
 * Parâmetros:
 *  - serverAddress (const char*): O endereço IP do servidor (exemplo: "192.168.1.1").
 *  - serverPort (int): A porta do servidor para conexão (exemplo: 8080).
 *
 * O que a função faz:
 *  1. Configura uma estrutura `sockaddr_in` com as informações do servidor:
 *     - Inicializa a estrutura com zeros usando `bzero`.
 *     - Define que será usado IPv4 (`AF_INET`).
 *     - Converte o endereço IP do formato string para binário com `inet_addr`.
 *     - Converte a porta para o formato adequado para rede usando `htons`.
 *  2. Cria um socket TCP com a função `socket`.
 *  3. Tenta se conectar ao servidor com a função `connect`:
 *     - Em caso de falha, fecha o socket e retorna `-1`.
 *  4. Retorna o descritor de arquivo do socket em caso de sucesso.
 *
 * Retornos:
 *  - Um número inteiro representando o socket (descritor de arquivo) se a conexão for bem-sucedida.
 *  - -1 se houver algum erro ao criar o socket ou conectar ao servidor.
 */

int connectToServer(const char *serverAddress, int serverPort)
{
    int sockfd;                     // Descritor do socket
    struct sockaddr_in server_addr; // Estrutura para armazenar informações do servidor

    // Configurando o endereço do servidor
    bzero((char *)&server_addr, sizeof(server_addr));       // Zera a estrutura
    server_addr.sin_family = AF_INET;                       // Define o tipo como IPv4
    server_addr.sin_addr.s_addr = inet_addr(serverAddress); // Converte o endereço IP
    server_addr.sin_port = htons(serverPort);               // Converte a porta para formato de rede

    // Criando o socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0); // Cria um socket TCP
    if (sockfd < 0)
    {
        perror("Erro ao criar o socket");
        return -1; // Retorna erro se não conseguir criar o socket
    }

    // Tentando conectar ao servidor
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Erro ao conectar ao servidor");
        close(sockfd); // Fecha o socket em caso de falha
        return -1;
    }

    return sockfd; // Retorna o socket se a conexão foi bem-sucedida
}

/*
 * closeConnection
 * ----------------
 * Essa função fecha uma conexão aberta.
 *
 * Parâmetros:
 *  - sockfd (int): O descritor de arquivo do socket a ser fechado.
 *
 * O que a função faz:
 *  - Usa a função `close` para encerrar a conexão.
 *  - Exibe uma mensagem de erro com `perror` caso o fechamento falhe.
 */

void closeConnection(int sockfd)
{
    if (close(sockfd) < 0)
    { // Tenta fechar o socket
        perror("Erro ao fechar o socket");
    }
}
