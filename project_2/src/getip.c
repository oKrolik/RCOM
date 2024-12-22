#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

/*
 * resolveHostnameToIP
 * --------------------
 * Essa função resolve o nome de um domínio (hostname) para o endereço IP correspondente.
 *
 * Parâmetros:
 *  - hostname (const char*): O nome do domínio a ser resolvido (exemplo: "www.google.com").
 *  - ipBuffer (char*): Um buffer onde o endereço IP será armazenado como string legível (exemplo: "142.250.217.68").
 *
 * O que a função faz:
 *  1. Usa `gethostbyname` para obter informações sobre o domínio fornecido:
 *     - Retorna uma estrutura contendo detalhes do host, incluindo o endereço IP.
 *     - Se não for possível resolver o hostname, exibe uma mensagem de erro e retorna `-1`.
 *  2. Converte o endereço IP do formato binário (estrutura `in_addr`) para uma string legível usando `inet_ntoa`.
 *  3. Copia a string resultante para o buffer `ipBuffer` usando `strcpy`.
 *
 * Retornos:
 *  - 0 em caso de sucesso, com o IP armazenado em `ipBuffer`.
 *  - -1 em caso de erro na resolução do hostname.
 */

int resolveHostnameToIP(const char *hostname, char *ipBuffer)
{
    struct hostent *h;

    // Resolve o hostname e obtém as informações do domínio
    h = gethostbyname(hostname);
    if (h == NULL)
    {
        herror("Erro ao resolver o hostname"); // Exibe mensagem de erro
        return -1;                             // Retorna erro
    }

    // Converte o endereço IP e armazena no buffer
    strcpy(ipBuffer, inet_ntoa(*((struct in_addr *)h->h_addr)));

    return 0; // Sucesso
}
