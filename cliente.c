/*
 * Fichero: cliente.c
 * Autores:
 *	Izan Jiménez Chaves DNI 71049459k
 *	Victor Haro Crespo DNI 76076364T
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

extern int errno;

#define ADDRNOTFOUND 0xffffffff /* value returned for unknown host */
#define RETRIES 5				/* number of times to retry before givin up */
#define BUFFERSIZE 1024			/* maximum size of packets to be received */
#define PUERTO 9459
#define TIMEOUT 6
#define MAXHOST 512
#define TAM_BUFFER 512

int main(int argc, char *argv[])
{
	// Variables comunes
	int s; /* connected socket descriptor */
	int i, errcode, addrlen;
	struct sockaddr_in myaddr_in;	/* for local socket address */
	struct sockaddr_in servaddr_in; /* for server socket address */
	struct addrinfo hints, *res;
	long timevar; /* contains time returned by time() */

	// Variables TCP
	int j;

	// Variables UDP
	int retry = RETRIES;
	struct in_addr reqaddr; 
	int n_retry;
	struct sigaction vec;
	char hostname[MAXHOST];
	char host[50];

	char buf[TAM_BUFFER];

	//!/////////////////////////////////////////////////////////////////////

	if (argc < 2 || (strcmp(argv[1], "TCP") != 0 && strcmp(argv[1], "UDP") != 0) || argc > 4)
	{
		fprintf(stderr, "Usage:  %s <tipo de protocolo (TCP/UDP)> (opc) <target>\n", argv[0]);
		exit(1);
	} // comprobacion de error
	else if (argc == 2)
	{
		strcpy(buf, "\r\n");
		strcpy(host, "localhost");
	} // si tenemos solo dos argumentos ej: ./cliente TCP
	else if (argc == 3)
	{
		if (strchr(argv[2], '@') == NULL) {
			//host = "localhost"; // Redirigir a localhost si no contiene '@'
			strcpy(host, "localhost");
			strcpy(buf, argv[2]);
			strcat(buf, "\r\n");

		} else {
			int i = 0;
			
			if (argv[2][0] == '@') {
				char *token = strtok(argv[2], "@");
				strcpy(host, token);
				strcpy(buf, "\r\n");

			} else {
				char *token = strtok(argv[2], "@");
				strcpy(buf, token);
				token = strtok(NULL, "@");
				strcpy(host, token);
			}

			printf("Host: %s\n", host);
			printf("Buffer: %s\n", buf);
		}

	}

	// -- Creamoh el sokeg
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to create socket\n", argv[0]);
		exit(1);
	}
	else
	{
		printf("Creado correctamente..\n");
	}

	//comparacion para saber si es UDP O TCP
	if (strcmp(argv[1], "TCP") == 0)
	{
		/* clear out address structures */
		memset((char *)&myaddr_in, 0, sizeof(struct sockaddr_in));
		memset((char *)&servaddr_in, 0, sizeof(struct sockaddr_in));

		/* Set up the peer address to which we will connect. */
		servaddr_in.sin_family = AF_INET;

		/* Get the host information for the hostname that the
		 * user passed in. */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		
		printf("Estamos aqui 1\n");
		/* esta funci�n es la recomendada para la compatibilidad con IPv6 gethostbyname queda obsoleta*/
		errcode = getaddrinfo(host, NULL, &hints, &res);
		if (errcode != 0)
		{
			/* Name was not found.  Return a special value signifying the error. */
			fprintf(stderr, "%s: No es posible resolver la IP de %s\n", argv[0], argv[1]);
			exit(1);
		}

		servaddr_in.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

		freeaddrinfo(res);

		/* puerto del servidor en orden de red*/
		servaddr_in.sin_port = htons(PUERTO);

		printf("Estamos aqui\n");
		if (connect(s, (const struct sockaddr *)&servaddr_in, sizeof(struct sockaddr_in)) == -1)
		{
			printf("Nos hemos cagado encima\n\n");
			perror(argv[0]);
			fprintf(stderr, "%s: unable to connect to remote\n", argv[0]);
			exit(1);
		}

		addrlen = sizeof(struct sockaddr_in);
		if (getsockname(s, (struct sockaddr *)&myaddr_in, &addrlen) == -1)
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to read socket address\n", argv[0]);
			exit(1);
		}

		/* Print out a startup message for the user. */
		time(&timevar);

		printf("Connected to %s on port %u at %s",
			   host, ntohs(myaddr_in.sin_port), (char *)ctime(&timevar));

		// enviamos la cadena con un tamanno de buffer igual su longitud
		if (send(s, buf, strlen(buf), 0) > TAM_BUFFER)
		{
			fprintf(stderr, "%s: Connection aborted on error ", argv[0]);
			fprintf(stderr, "on send number %d\n", i);
			exit(1);
		}

		// printf("Enviado correctamente cerrando escritura\n");
		// /*
		// if (shutdown(s, 1) == -1) // CERRAMOS UNICAMENTE LA ESCRITURA DEL CLIENTE
		// {
		// 	perror(argv[0]);
		// 	fprintf(stderr, "%s: unable to shutdown socket\n", argv[0]);
		// 	exit(1);
		// }
		// */
		// //sleep(1);
		// strcpy(buf, "awadecochinillo\r\n");
		// if (send(s, buf, strlen(buf), 0) > TAM_BUFFER)
		// {
		// 	fprintf(stderr, "%s: Connection aborted on error ", argv[0]);
		// 	fprintf(stderr, "on send number %d\n", i);
		// 	exit(1);
		// }

		if (shutdown(s, 1) == -1) // CERRAMOS UNICAMENTE LA ESCRITURA DEL CLIENTE
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to shutdown socket\n", argv[0]);
			exit(1);
		}
		int flag = 1;
		printf("Cerrada la escriturax2\n");

		while (i = recv(s, buf, TAM_BUFFER, 0))
		{
			if (i == -1)
			{
				perror(argv[0]);
				fprintf(stderr, "%s: error reading result\n", argv[0]);
				exit(1);
			}

			int length = strlen(buf);
			if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
			{
				flag = 0;
			}

			while (i < TAM_BUFFER || flag == 1)
			{
				j = recv(s, &buf[i], TAM_BUFFER - i, 0);
				if (j == -1)
				{
					perror(argv[0]);
					fprintf(stderr, "%s: error reading result\n", argv[0]);
					exit(1);
				}
				i += j;
			}
			/* Print out message indicating the identity of this reply. */
			printf("Hemos recibido esto de servidor:\n%s\n", buf);
		}

		/* Print message indicating completion of task. */
		time(&timevar);
		printf("Cliente: All done at %s", (char *)ctime(&timevar));
	}
	// AQUI EMPIEZA UDP
	else
	{

	}
}
