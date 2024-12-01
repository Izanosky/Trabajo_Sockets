/*
 * Fichero: servidor.c
 * Autores:
 *	Izan Jiménez Chaves DNI 71049459k
 *	Victor Haro Crespo DNI 76076364T
 *
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PUERTO 9459
#define ADDRNOTFOUND 0xffffffff /* return address for unfound host */
#define BUFFERSIZE 1024			/* maximum size of packets to be received */
#define TAM_BUFFER 512
#define MAXHOST 128

extern int errno;

/*
 *			M A I N
 *
 *	This routine starts the server.  It forks, leaving the child
 *	to do all the work, so it does not have to be run in the
 *	background.  It sets up the sockets.  It
 *	will loop forever, until killed by a signal.
 *
 */

void serverTCP(int s, struct sockaddr_in peeraddr_in);
void serverUDP(int s, char *buffer, struct sockaddr_in clientaddr_in);
void errout(char *); /* declare error out routine */

int FIN = 0; /* Para el cierre ordenado */
void finalizar() { FIN = 1; }

int main(int argc, char *argv[])
{

	int s_TCP, s_UDP; /* connected socket descriptor */
	int ls_TCP;		  /* listen socket descriptor */

	int cc; /* contains the number of bytes read */

	struct sigaction sa = {.sa_handler = SIG_IGN}; /* used to ignore SIGCHLD */

	struct sockaddr_in myaddr_in;	  /* for local socket address */
	struct sockaddr_in clientaddr_in; /* for peer socket address */
	int addrlen;

	fd_set readmask;
	int numfds, s_mayor;

	char buffer[BUFFERSIZE]; /* buffer for packets to be read into */

	struct sigaction vec;

	/* Create the listen socket. */
	ls_TCP = socket(AF_INET, SOCK_STREAM, 0);
	if (ls_TCP == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to create socket TCP\n", argv[0]);
		exit(1);
	}
	/* clear out address structures */
	memset((char *)&myaddr_in, 0, sizeof(struct sockaddr_in));
	memset((char *)&clientaddr_in, 0, sizeof(struct sockaddr_in));

	addrlen = sizeof(struct sockaddr_in);

	/* Set up address structure for the listen socket. */
	myaddr_in.sin_family = AF_INET;
	/* The server should listen on the wildcard address,
	 * rather than its own internet address.  This is
	 * generally good practice for servers, because on
	 * systems which are connected to more than one
	 * network at once will be able to have one server
	 * listening on all networks at once.  Even when the
	 * host is connected to only one network, this is good
	 * practice, because it makes the server program more
	 * portable.
	 */
	myaddr_in.sin_addr.s_addr = INADDR_ANY; // cualquier direccion
	myaddr_in.sin_port = htons(PUERTO);

	/* Bind the listen address to the socket. */
	if (bind(ls_TCP, (const struct sockaddr *)&myaddr_in, sizeof(struct sockaddr_in)) == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to bind address TCP\n", argv[0]);
		exit(1);
	}
	/* Initiate the listen on the socket so remote users
	 * can connect.  The listen backlog is set to 5, which
	 * is the largest currently supported.
	 */
	if (listen(ls_TCP, 5) == -1)
	{
		perror(argv[0]);
		fprintf(stderr, "%s: unable to listen on socket\n", argv[0]);
		exit(1);
	}

	/* Create the socket UDP. */
	s_UDP = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_UDP == -1)
	{
		perror(argv[0]);
		printf("%s: unable to create socket UDP\n", argv[0]);
		exit(1);
	}
	/* Bind the server's address to the socket. */
	if (bind(s_UDP, (struct sockaddr *)&myaddr_in, sizeof(struct sockaddr_in)) == -1)
	{
		perror(argv[0]);
		printf("%s: unable to bind address UDP\n", argv[0]);
		exit(1);
	}

	/* Now, all the initialization of the server is
	 * complete, and any user errors will have already
	 * been detected.  Now we can fork the daemon and
	 * return to the user.  We need to do a setpgrp
	 * so that the daemon will no longer be associated
	 * with the user's control terminal.  This is done
	 * before the fork, so that the child will not be
	 * a process group leader.  Otherwise, if the child
	 * were to open a terminal, it would become associated
	 * with that terminal as its control terminal.  It is
	 * always best for the parent to do the setpgrp.
	 */
	setpgrp();

	switch (fork())
	{
	case -1: /* Unable to fork, for some reason. */
		perror(argv[0]);
		fprintf(stderr, "%s: unable to fork daemon\n", argv[0]);
		exit(1);

	case 0: /* The child process (daemon) comes here. */

		/* Close stdin and stderr so that they will not
		 * be kept open.  Stdout is assumed to have been
		 * redirected to some logging file, or /dev/null.
		 * From now on, the daemon will not report any
		 * error messages.  This daemon will loop forever,
		 * waiting for connections and forking a child
		 * server to handle each one.
		 */
		fclose(stdin);
		fclose(stderr);

		/* Set SIGCLD to SIG_IGN, in order to prevent
		 * the accumulation of zombies as each child
		 * terminates.  This means the daemon does not
		 * have to make wait calls to clean them up.
		 */
		if (sigaction(SIGCHLD, &sa, NULL) == -1)
		{
			perror(" sigaction(SIGCHLD)");
			fprintf(stderr, "%s: unable to register the SIGCHLD signal\n", argv[0]);
			exit(1);
		}

		/* Registrar SIGTERM para la finalizacion ordenada del programa servidor */
		vec.sa_handler = (void *)finalizar;
		vec.sa_flags = 0;
		if (sigaction(SIGTERM, &vec, (struct sigaction *)0) == -1)
		{
			perror(" sigaction(SIGTERM)");
			fprintf(stderr, "%s: unable to register the SIGTERM signal\n", argv[0]);
			exit(1);
		}

		while (!FIN)
		{
			/* Meter en el conjunto de sockets los sockets UDP y TCP */
			FD_ZERO(&readmask);
			FD_SET(ls_TCP, &readmask);
			FD_SET(s_UDP, &readmask);
			/*
			Seleccionar el descriptor del socket que ha cambiado. Deja una marca en
			el conjunto de sockets (readmask)
			*/
			if (ls_TCP > s_UDP)
				s_mayor = ls_TCP;
			else
				s_mayor = s_UDP;

			if ((numfds = select(s_mayor + 1, &readmask, (fd_set *)0, (fd_set *)0, NULL)) < 0)
			{
				if (errno == EINTR)
				{
					FIN = 1;
					close(ls_TCP);
					close(s_UDP);
					perror("\nFinalizando el servidor. Se�al recibida en elect\n ");
				}
			}
			else
			{

				/* Comprobamos si el socket seleccionado es el socket TCP */
				if (FD_ISSET(ls_TCP, &readmask))
				{
					/* Note that addrlen is passed as a pointer
					 * so that the accept call can return the
					 * size of the returned address.
					 */
					/* This call will block until a new
					 * connection arrives.  Then, it will
					 * return the address of the connecting
					 * peer, and a new socket descriptor, s,
					 * for that connection.
					 */
					s_TCP = accept(ls_TCP, (struct sockaddr *)&clientaddr_in, &addrlen);
					if (s_TCP == -1)
						exit(1);
					switch (fork())
					{
					case -1: /* Can't fork, just exit. */
						exit(1);
					case 0:			   /* Child process comes here. */
						close(ls_TCP); /* Close the listen socket inherited from the daemon. */
						serverTCP(s_TCP, clientaddr_in);
						exit(0);
					default: /* Daemon process comes here. */
							 /* The daemon needs to remember
							  * to close the new accept socket
							  * after forking the child.  This
							  * prevents the daemon from running
							  * out of file descriptor space.  It
							  * also means that when the server
							  * closes the socket, that it will
							  * allow the socket to be destroyed
							  * since it will be the last close.
							  */
						close(s_TCP);
					}
				} /* De TCP*/
				/* Comprobamos si el socket seleccionado es el socket UDP */
				if (FD_ISSET(s_UDP, &readmask))
				{
					/* This call will block until a new
					 * request arrives.  Then, it will
					 * return the address of the client,
					 * and a buffer containing its request.
					 * BUFFERSIZE - 1 bytes are read so that
					 * room is left at the end of the buffer
					 * for a null character.
					 */
					cc = recvfrom(s_UDP, buffer, BUFFERSIZE - 1, 0,
								  (struct sockaddr *)&clientaddr_in, &addrlen);
					if (cc == -1)
					{
						perror(argv[0]);
						printf("%s: recvfrom error\n", argv[0]);
						exit(1);
					}
					/* Make sure the message received is
					 * null terminated.
					 */
					buffer[cc] = '\0';
					serverUDP(s_UDP, buffer, clientaddr_in);
				}
			}
		} /* Fin del bucle infinito de atenci�n a clientes */
		/* Cerramos los sockets UDP y TCP */
		close(ls_TCP);
		close(s_UDP);

		printf("\nFin de programa servidor!\n");

	default: /* Parent process comes here. */
		exit(0);
	}
}

/*
 *				S E R V E R T C P
 *
 *	This is the actual server routine that the daemon forks to
 *	handle each individual connection.  Its purpose is to receive
 *	the request packets from the remote client, process them,
 *	and return the results to the client.  It will also write some
 *	logging information to stdout.
 *
 */
void serverTCP(int s, struct sockaddr_in clientaddr_in)
{
	int reqcnt = 0;
	char buf[TAM_BUFFER];
	char hostname[MAXHOST];

	int len, len1, status;
	struct hostent *hp;
	long timevar;

	struct linger linger;

	status = getnameinfo((struct sockaddr *)&clientaddr_in, sizeof(clientaddr_in),
						 hostname, MAXHOST, NULL, 0, 0);
	if (status)
	{
		if (inet_ntop(AF_INET, &(clientaddr_in.sin_addr), hostname, MAXHOST) == NULL)
			perror(" inet_ntop \n");
	}

	time(&timevar);

	printf("Startup from %s port %u at %s",
		   hostname, ntohs(clientaddr_in.sin_port), (char *)ctime(&timevar));

	linger.l_onoff = 1;
	linger.l_linger = 1;
	if (setsockopt(s, SOL_SOCKET, SO_LINGER, &linger,
				   sizeof(linger)) == -1)
	{
		errout(hostname);
	}

	// bucle de recepcion de requests
	int flag = 1;

	while (len = recv(s, buf, TAM_BUFFER, 0))
	{
		if (len == -1)
		{
			errout(hostname);
		}

		if (len == 0)
		{
			printf("Se ha cerrado la escritura while1\n");
			break;
		}

		int length = strlen(buf);
		if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
		{
			flag = 0;
			printf("ta bn");
		}
		else
		{
			printf("ta mal");
			flag = 1;
		}

		while (flag != 0 && len < TAM_BUFFER)
		{
			len1 = recv(s, &buf[len], TAM_BUFFER - len, 0);
			
			if (len1 == -1) errout(hostname);
			
			else if (len1 == 0)
			{
				printf("Se ha cerrado la escritura while 2\n");
				break;
			}

			len += len1;
			printf("\n\nlen1: %d\n", len1);
			// sleep(1);
			length = strlen(buf);
			if (buf[length - 1] == '\n' && buf[length - 2] == '\r')
			{
				flag = 0;
			}
		}

		printf("\n\n\nHola buenas tardes\n\n\n");

		if (strcmp(buf, "\r\n\0") == 0)
		{
			if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
				errout(hostname);
		}
		
		else
		{
			char usr[TAM_BUFFER];
			strcpy(usr, buf);
			usr[strcspn(usr, "\r\n")] = '\0';
			printf("Request: %s\n", usr);

			system("getent passwd > ./aux.txt");

			FILE *fp;
			char info[512];

			if ((fp = fopen("aux.txt", "r")) == NULL)
			{
				perror("Error al ejecutar last");
				exit(1);
			}

			// Limpiar el buffer
			buf[0] = '\0';
			
			printf("Sesiones que contienen '%s': \n", usr);

			while (fgets(info, sizeof(info), fp) != NULL)
			{
				// Eliminar saltos de línea
				info[strcspn(info, "\r\n")] = '\0';

				// Buscar coincidencias
				if (strstr(info, usr) != NULL)
				{
					printf("info %s\n", info);

					// Concatenar al buffer, verificando que no se desborde
					if (strlen(buf) + strlen(info) + 2 < TAM_BUFFER)
					{
						strcat(buf, info);
						strcat(buf, "\r\n"); // Añadir terminación de línea
					}
					else
					{
						// Enviar el buffer actual si está lleno
						if (send(s, buf, strlen(buf), 0) != strlen(buf))
						{
							errout("hostname");
						}
						// Reiniciar el buffer y añadir la nueva línea
						snprintf(buf, TAM_BUFFER, "%s", info);
					}
				}
			}

			if(strlen(buf) == 0)
			{
				strcpy(buf, "No se han encontrado coincidencias\r\n");
			}

			fclose(fp);
		}

		/* Increment the request count. */
		reqcnt++;
		/* This sleep simulates the processing of the
		 * request that a real server might do.
		 */
		sleep(1);
		strcat(buf, "\r\n");
		/* Send a response back to the client. */
		if (send(s, buf, TAM_BUFFER, 0) != TAM_BUFFER)
			errout(hostname);
	}

	close(s);

	time(&timevar);

	printf("Servidor: Completed %s port %u, %d requests, at %s\n",
		   hostname, ntohs(clientaddr_in.sin_port), reqcnt, (char *)ctime(&timevar));
}

/*
 *	This routine aborts the child process attending the client.
 */
void errout(char *hostname)
{
	printf("Connection with %s aborted on error\n", hostname);
	exit(1);
}

/*
 *				S E R V E R U D P
 *
 *	This is the actual server routine that the daemon forks to
 *	handle each individual connection.  Its purpose is to receive
 *	the request packets from the remote client, process them,
 *	and return the results to the client.  It will also write some
 *	logging information to stdout.
 *
 */
void serverUDP(int s, char *buffer, struct sockaddr_in clientaddr_in)
{
	struct in_addr reqaddr; /* for requested host's address */
	struct hostent *hp;		/* pointer to host info for requested host */
	int nc, errcode;

	struct addrinfo hints, *res;

	int addrlen;

	addrlen = sizeof(struct sockaddr_in);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	/* Treat the message as a string containing a hostname. */
	/* Esta funci�n es la recomendada para la compatibilidad con IPv6 gethostbyname queda obsoleta. */
	errcode = getaddrinfo(buffer, NULL, &hints, &res);
	if (errcode != 0)
	{
		/* Name was not found.  Return a
		 * special value signifying the error. */
		reqaddr.s_addr = ADDRNOTFOUND;
	}
	else
	{
		/* Copy address of host into the return buffer. */
		reqaddr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
	}
	freeaddrinfo(res);

	nc = sendto(s, &reqaddr, sizeof(struct in_addr),
				0, (struct sockaddr *)&clientaddr_in, addrlen);
	if (nc == -1)
	{
		perror("serverUDP");
		printf("%s: sendto error\n", "serverUDP");
		return;
	}
}
