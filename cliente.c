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
#define TIMEOUT 5
#define MAXHOST 512
#define TAM_BUFFER 516

volatile sig_atomic_t timeout_flag = 0;

void handler()
{
	printf("Alarma recibida \n");
	timeout_flag = 1;
}

int main(int argc, char *argv[])
{
	// Variables comunes
	int s; /* connected socket descriptor */
	int i, errcode, addrlen;
	struct sockaddr_in myaddr_in;	/* for local socket address */
	struct sockaddr_in servaddr_in; /* for server socket address */
	struct addrinfo hints, *res;
	FILE *f;
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

	char login[50] = "", name[100] = "", directory[100] = "", shell[50] = "";
	char terminal[50] = "N/A", ip[50] = "N/A", fecha[100] = "Never logged in.";

	//!/////////////////////////////////////////////////////////////////////
	memset(buf, 0, TAM_BUFFER);
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
		if (strchr(argv[2], '@') == NULL)
		{
			// host = "localhost"; // Redirigir a localhost si no contiene '@'
			strcpy(host, "localhost");
			strcpy(buf, argv[2]);
			strcat(buf, "\r\n");
		}
		else
		{
			int i = 0;

			if (argv[2][0] == '@')
			{
				char *token = strtok(argv[2], "@");
				strcpy(host, token);
				strcpy(buf, "\r\n");
			}
			else
			{
				char *token = strtok(argv[2], "@");
				strcpy(buf, token);
				token = strtok(NULL, "@");
				strcpy(host, token);
			}
		}
	}

	// comparacion para saber si es UDP O TCP
	if (strcmp(argv[1], "TCP") == 0)
	{
		// -- Creamos el socket
		s = socket(AF_INET, SOCK_STREAM, 0);
		if (s == -1)
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to create socket\n", argv[0]);
			exit(1);
		}
		/* clear out address structures */
		memset((char *)&myaddr_in, 0, sizeof(struct sockaddr_in));
		memset((char *)&servaddr_in, 0, sizeof(struct sockaddr_in));

		/* Set up the peer address to which we will connect. */
		servaddr_in.sin_family = AF_INET;

		/* Get the host information for the hostname that the
		 * user passed in. */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;

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

		if (connect(s, (const struct sockaddr *)&servaddr_in, sizeof(struct sockaddr_in)) == -1)
		{
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

		// enviamos la cadena con un tamanno de buffer igual su longitud
		if (send(s, buf, strlen(buf), 0) > TAM_BUFFER)
		{
			fprintf(stderr, "%s: Connection aborted on error ", argv[0]);
			fprintf(stderr, "on send number %d\n", i);
			exit(1);
		}

		if (shutdown(s, 1) == -1) // CERRAMOS UNICAMENTE LA ESCRITURA DEL CLIENTE
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to shutdown socket\n", argv[0]);
			exit(1);
		}
		int flag = 1;

		// limpiamos el buffer
		memset(buf, 0, TAM_BUFFER);
		memset(fecha, 0, 100);
		memset(name, 0, 100);

		char token[50];
		snprintf(token, sizeof(token), "%d.txt", ntohs(myaddr_in.sin_port));
		f = fopen(token, "w");
		if (f == NULL)
		{
			printf("Error opening file!\n");
			if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
				exit(1);
		}

		while (i = recv(s, buf, TAM_BUFFER, 0))
		{
			if (i == -1)
			{
				perror(argv[0]);
				fprintf(stderr, "%s: error reading result\n", argv[0]);
				exit(1);
			}

			if (i > 2 && buf[strlen(buf) - 1] == '\n' && buf[strlen(buf) - 2] == '\r')
			{
				flag = 0;
			}
			else
			{
				flag = 1;
			}

			while (flag != 0 && i < TAM_BUFFER)
			{
				j = recv(s, &buf[i], TAM_BUFFER - i, 0);

				if (j == -1)
					printf("Error on client\n");
				exit(1);

				i += j;
				// sleep(1);
				int length = strlen(buf);
				if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
				{
					flag = 0;
				}
			}

			buf[i] = '\0';

			int length = strlen(buf);
			if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
			{
				fprintf(f, "%s", buf);
				if (strcmp(buf, "No existe el usuario\r\n") == 0)
				{
					printf("\nNo existe el usuario\n");
					continue;
				}

				char *token = strtok(buf, "|");
				if (token)
					strcpy(login, token);
				strcat(login, "\0");

				token = strtok(NULL, "|");
				if (token)
					strcpy(name, token);
				strcat(name, "\0");

				token = strtok(NULL, "|");
				if (token)
					strcpy(directory, token);
				strcat(directory, "\0");

				token = strtok(NULL, "|");
				if (token)
					strcpy(shell, token);
				strcat(shell, "\0");

				token = strtok(NULL, "|");
				if (token && strlen(token) > 0)
					strcpy(terminal, token);
				strcat(terminal, "\0");

				token = strtok(NULL, "|");
				if (token && strlen(token) > 0)
					strcpy(ip, token);
				strcat(ip, "\0");

				token = strtok(NULL, "|");
				if (token && strlen(token) > 0)
					strcpy(fecha, token);

				puts("");
				if (strcmp(login, "\r\n") == 0)
				{
					printf("\nNo existe el usuario\n");
				}
				else
				{
					// Imprimir en formato tipo `finger`
					printf("Login: %-20s Name: %s\n", login, name);
					printf("Directory: %-15s Shell: %s\n", directory, shell);
					if (strlen(fecha) > 0)
					{
						// Si la fecha está disponible, imprimir toda la información
						printf("On since: %s on %s from %s\n\n\n", fecha, terminal, ip);
					}
					else
					{
						// Si la fecha está vacía, imprimir el mensaje "Never logged in"
						printf("Never logged in.\n\n\n");
					}
				}

				memset(fecha, 0, 100);
				memset(name, 0, 100);
				// break;
				// printf("Hemos recibido esto de servidor:\n%s\n", buf);
			}

			/* Print out message indicating the identity of this reply. */
		}

		fclose(f);

		/* Print message indicating completion of task. */
		time(&timevar);
		// printf("Cliente: All done at %s", (char *)ctime(&timevar));
	}

	// AQUI EMPIEZA UDP ------------------------------------------------------------------------------------
	else if (strcmp(argv[1], "UDP") == 0)
	{
		// -- Creamos el socket
		s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == -1)
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to create socket\n", argv[0]);
			exit(1);
		}
		memset((char *)&myaddr_in, 0, sizeof(struct sockaddr_in));
		memset((char *)&servaddr_in, 0, sizeof(struct sockaddr_in));

		myaddr_in.sin_family = AF_INET;
		myaddr_in.sin_port = 0;
		myaddr_in.sin_addr.s_addr = INADDR_ANY;

		if (bind(s, (const struct sockaddr *)&myaddr_in, sizeof(struct sockaddr_in)) == -1)
		{
			perror(argv[0]);
			fprintf(stderr, "%s: unable to bind socket\n", argv[0]);
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

		printf("Connected to %s on port %u at %s", host, ntohs(myaddr_in.sin_port), (char *)ctime(&timevar));

		servaddr_in.sin_family = AF_INET;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;

		errcode = getaddrinfo(host, NULL, &hints, &res);
		if (errcode != 0)
		{
			fprintf(stderr, "%s: No es posible resolver la IP de %s\n",
					argv[0], argv[1]);
			exit(1);
		}
		else
		{
			servaddr_in.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
		}
		freeaddrinfo(res);

		servaddr_in.sin_port = htons(PUERTO);

		vec.sa_handler = (void *)handler;
		vec.sa_flags = 0;
		if (sigaction(SIGALRM, &vec, (struct sigaction *)0) == -1)
		{
			perror(" sigaction(SIGALRM)");
			fprintf(stderr, "%s: unable to register the SIGALRM signal\n", argv[0]);
			exit(1);
		}

		n_retry = RETRIES;

		int len;
		char token[50];
		snprintf(token, sizeof(token), "%d.txt", ntohs(myaddr_in.sin_port));
		f = fopen(token, "w");
		if (f == NULL)
		{
			printf("Error opening file!\n");
			if (send(s, "Error opening file!\r\n", TAM_BUFFER, 0) != TAM_BUFFER)
				exit(1);
		}

		int flag = 1;

		while (n_retry > 0)
		{	
			
			/* Send the request to the nameserver. */
			if (flag == 1 && sendto(s, buf, strlen(buf), 0, (struct sockaddr *)&servaddr_in,
					   sizeof(struct sockaddr_in)) == -1)
			{
				perror(argv[0]);
				fprintf(stderr, "%s: unable to send request\n", argv[0]);
				exit(1);
			}

			alarm(TIMEOUT);

			timeout_flag = 0;

			while (!timeout_flag)
			{
				// memset(buf, 0, TAM_BUFFER);

				if ((len = recvfrom(s, buf, TAM_BUFFER, 0, (struct sockaddr *)&servaddr_in, &addrlen)) == -1)
				{
					if (errno == EINTR)
					{
						printf("attempt %d (retries %d).\n", n_retry, RETRIES);
						n_retry--;
					}
					else
					{
						printf("Unable to get response from");
						exit(1);
					}
				}
				else
				{
					if (reqaddr.s_addr == ADDRNOTFOUND)
						printf("Host %s unknown\n", host);
					else
					{
						if (flag == 0) continue;
						flag = 0;
						int length = strlen(buf);
						if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
						{
							fprintf(f, "%s", buf);
							if (strcmp(buf, "No existe el usuario\r\n") == 0)
							{
								printf("\nNo existe el usuario\n");
								break;
							}

							char *token = strtok(buf, "|");
							if (token)
								strcpy(login, token);
							strcat(login, "\0");

							token = strtok(NULL, "|");
							if (token)
								strcpy(name, token);
							strcat(name, "\0");

							token = strtok(NULL, "|");
							if (token)
								strcpy(directory, token);
							strcat(directory, "\0");

							token = strtok(NULL, "|");
							if (token)
								strcpy(shell, token);
							strcat(shell, "\0");

							token = strtok(NULL, "|");
							if (token && strlen(token) > 0)
								strcpy(terminal, token);
							strcat(terminal, "\0");

							token = strtok(NULL, "|");
							if (token && strlen(token) > 0)
								strcpy(ip, token);
							strcat(ip, "\0");

							token = strtok(NULL, "|");
							if (token && strlen(token) > 0)
								strcpy(fecha, token);

							puts("");
							// Imprimir en formato tipo `finger`
							printf("Login: %-20s Name: %s\n", login, name);
							printf("Directory: %-15s Shell: %s\n", directory, shell);
							if (strlen(fecha) > 0)
							{
								// Si la fecha está disponible, imprimir toda la información
								printf("On since: %s on %s from %s\n\n\n", fecha, terminal, ip);
							}
							else
							{
								// Si la fecha está vacía, imprimir el mensaje "Never logged in"
								printf("Never logged in.\n\n\n");
							}


							memset(fecha, 0, 100);
							memset(name, 0, 100);
						}

						if (n_retry == 0)
						{
							printf("Unable to get response from");
							printf(" %s after %d attempts.\n", argv[1], RETRIES);
						}
					}
				}
			}
		}
	}
}