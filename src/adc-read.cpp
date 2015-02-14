// adc-read: Read the ADC port(s) on the BeagleBone Black, wrap the
// resultant data into a UDP packet, and send it to the specified
// host and port
//
// Copyright (C) 2015  Overclocked

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// For a copy of the GNU General Public License, see LICENSE in the
// repository's root directory.

// Must be run as root

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <termios.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <cstdint>
#include <cmath>
#include <sys/errno.h>
#include <time.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>


using namespace std;

bool verbose = false;

struct sockaddr targetSockAddr; 
int createUdpSocket(char *serverName, short port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sock, s;
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		throw -1;
	}
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	
	char portBuf[16];
	memset(portBuf, 0, sizeof(portBuf));
	sprintf(portBuf, "%d", port);
	s = getaddrinfo(serverName, portBuf , &hints, &result);
	if (s != 0) {
		cerr << "getaddrinfo failed: " << gai_strerror(s) << endl;
		throw -1;
	}
	// getaddrinfo returns a list of address structures
	// Try each address until we successfully bind(2).
	// If socket(2) or bind(2) fails, we close the socket
	// and try the next address
	char host_ip[20];
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock > 0) {
			inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), host_ip, 20);
			if (verbose) cout << "UDP socket to " << host_ip << " established" << endl;
			memcpy(&targetSockAddr, rp->ai_addr, rp->ai_addrlen);
			break;
		}
		//if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
		//inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), str, 20);
		//cerr << "Failed to bind to " << str << " on port " << ((struct sockaddr_in *)rp->ai_addr)->sin_port << endl;
		//close(sock);
	}
	if (rp == NULL) {
		freeaddrinfo(result);
		cerr << "Could not get address info for host " << serverName << endl;
		throw -1;
	}
	freeaddrinfo(result);
	
	return sock;
}

void Usage() {
  std::cerr << "Usage: adc-read -h <host> -p <port> [-v]" << endl
            << "Where: <host> is either the IP address or mDNS name of a host that's expecting" << endl
            << "              to receive parsed messages on the speficied <port>." << endl
            << "       <port> should be a number in the range of 5800-5810 inclusive, in order" << endl
            << "              to comply with FRC 2015 rules" << endl
            << "           -v (optional) run in Verbose mode, printing some debug information" << endl
            << "              to the standard output" << endl;
}

int adc[8];
int sock;
unsigned short transmitBuf[16];

void callback(union sigval arg) {
	char buf[30];
	for (int i = 0; i < 8; i++) {
		memset(buf, 0, sizeof(buf));
		// TODO: Read the ADC port(s) here and send the data via UDP
		lseek(adc[i], 0, SEEK_SET);
		int result = read(adc[i], buf, 4);
		transmitBuf[i * 2] = i + 1;
		transmitBuf[(i * 2) + 1] = (unsigned short)atoi(buf);
	}
	if(sendto(sock, transmitBuf, 32, 0, (struct sockaddr *)&targetSockAddr, sizeof(targetSockAddr)) != 32) {
		perror("Mismatch in number of bytes sent");
	}
}

timer_t timerid;
int main(int argc, char **argv) {
	int opt;
	char host[32];
	int port = -1;
	long int seconds = -1;
	long int nanoseconds = -1;
	
	double frequencyHz, timerPeriodSeconds, timerPeriodNanoseconds;
	
	memset(host, 0, sizeof(host));
	memset(adc, 0xFF, sizeof(adc));
  while ((opt = getopt(argc, argv, "vf:h:p:")) != -1) {
  	switch (opt) {
			case 'h':
				strncpy(host, optarg, sizeof(host) - 1);
				cout << "Host: " << host << endl;
				break;
			case 'p':
				port = atoi(optarg);
				cout << "Port: " << (int)port << endl;
				break;
			case 'f':
				frequencyHz = atof(optarg);
				timerPeriodNanoseconds = modf(1.0 / frequencyHz, &timerPeriodSeconds);
				timerPeriodNanoseconds *= 1e9;
				seconds = timerPeriodSeconds;
				nanoseconds = timerPeriodNanoseconds;
				cout << "Timer frequency: " << frequencyHz << " Hz. Period: " 
						 << seconds << " sec + " << nanoseconds << " nsec" << endl;
				break;
			case 'v':
				verbose = true;
				break;
			default:
				cerr << "Unknown option: '-" << (char)opt << "'" << endl;
				Usage();
				exit(-1);
				break;
  	}
  }
  if (host[0] == '\0' || port < 0 || port > 32767) {
  	Usage();
  	exit(-1);
  }

	if (verbose) {
		cout << "Target Host: " << host << ":" << port << endl;
	}
	//exit(1);
  //MulticastClient *client = new MulticastClient((char *)"226.0.0.1", (short)5800, (char *)"eth0");
  sock = createUdpSocket((char *)host, port);

	for (int i = 0; i < 8; i++) {
		char device[128];
		sprintf(device, "/sys/bus/iio/devices/iio:device0/in_voltage%d_raw", i);
		adc[i] = open(device, O_RDONLY);
		if (adc[i] < 0) {
			char error[64];
			sprintf(error, "Could not open port ADC%d", i);
			perror(error);
			return -1;
		}
  }

	struct sigevent se;
	struct itimerspec ts;

	se.sigev_notify = SIGEV_THREAD;
	se.sigev_value.sival_ptr = &timerid;
	se.sigev_notify_function = callback;
	se.sigev_notify_attributes = NULL;

	if (-1 == timer_create(CLOCK_REALTIME, &se, &timerid)) {
		perror("timer_create:");
		return(1);
	}

	ts.it_value.tv_sec = seconds;
	ts.it_value.tv_nsec = nanoseconds;
	ts.it_interval.tv_sec = seconds;
	ts.it_interval.tv_nsec = nanoseconds;

	if (-1 == timer_settime(timerid, 0, &ts, NULL)) {
		perror("timer_settime:");
	}

	try {
		while (true) sleep(10);
	}  
  catch (int e) {
    cerr << "Caught an exception: " << e << endl;
    return -1;
  }
}

