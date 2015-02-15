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

const int MAX_HOSTSTRING_LENGTH = 32;
const int MAX_PORTSTRING_LENGTH = 8;
const int MAX_ENDPOINT_COUNT = 10;
struct udp_endpoint {
	char hostString[MAX_HOSTSTRING_LENGTH];
	char portString[MAX_PORTSTRING_LENGTH];
	struct sockaddr sockAddr;
	int socket;
};

struct udp_endpoint adcEndpoints[MAX_ENDPOINT_COUNT];
int adcEndpointCount = 0;

void initialize_udp_endpoint(struct udp_endpoint *endpoint) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int s;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	s = getaddrinfo(endpoint->hostString, endpoint->portString, &hints, &result);
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
		int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sock > 0) {
			inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), host_ip, 20);
			if (verbose) cout << "UDP socket to " << host_ip << " created" << endl;
			endpoint->socket = sock;
			memcpy(&endpoint->sockAddr, rp->ai_addr, rp->ai_addrlen);
			break;
		}
		//if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0) break;
		//inet_ntop(AF_INET, &(((struct sockaddr_in *)rp->ai_addr)->sin_addr), str, 20);
		//cerr << "Failed to bind to " << str << " on port " << ((struct sockaddr_in *)rp->ai_addr)->sin_port << endl;
		//close(sock);
	}
	if (rp == NULL) {
		freeaddrinfo(result);
		cerr << "Could not get address info for host " << endpoint->hostString << endl;
		throw -1;
	}
	freeaddrinfo(result);
}

void Usage() {
  std::cerr << "Usage: adc-read -h <host> -f <frequency> [-v] [-n <adcPortCount>]" << endl
            << "Where: <host-port-pair> are pairs of either the IP address or mDNS name of" << endl
	    << "              hosts and corresponding ports that are expecting" << endl
            << "              to receive parsed messages on the specified port." << endl
	    << "              an example would be laptop1.local:5800,roborio-local:5801" << endl
            << "<adcPortCount> is the number of ADC ports that will be read" << endl
	    << "              on the local BeagleBone Black. Ports are always read" <<  endl
	    << "              consecutively starting with ADC0" << endl
	    << "           -f Poll the ADC ports at the specified frequency" << endl
	    << "           -n Number of ports to poll, always starting on ADC0" << endl
            << "           -v (optional) run in Verbose mode, printing some debug information" << endl
            << "              to the standard output" << endl;
}

int parseHostPortPairList(char *inputList, struct udp_endpoint *endpoints) {
	char *curPair, *pairContext;
	char *hostBuf, *portBuf, *hostContext;
	int count = 0;

	curPair = inputList;
	for (int i = 0; i < MAX_ENDPOINT_COUNT; i++) {
		curPair = strtok_r(inputList, ",", &pairContext);
		inputList = NULL;
		if (curPair == NULL) break;
		hostBuf = strtok_r(curPair, ":", &hostContext);
		portBuf = strtok_r(NULL, ":", &hostContext);
		if (hostBuf == NULL || portBuf == NULL) return -1;
		strncpy(endpoints[i].hostString, hostBuf, MAX_HOSTSTRING_LENGTH);
		strncpy(endpoints[i].portString, portBuf, MAX_PORTSTRING_LENGTH);
		count++;
	} 
	return count;
}

const int ADC_PORTS_TO_READ = 8;
int adc[8];
int sock;
uint8_t transmitBuf[32];
uint8_t adcCount = -1;

void callback(union sigval arg) {
	char buf[30];
	transmitBuf[0] = adcCount;
	for (int i = 0; i < adcCount; i++) {
		memset(buf, 0, sizeof(buf));
		// TODO: Read the ADC port(s) here and send the data via UDP
		lseek(adc[i], 0, SEEK_SET);
		read(adc[i], buf, 4);
		transmitBuf[(i * 3) + 1] = (uint8_t)(i + 1);
		uint16_t sample = (unsigned short)atoi(buf);
		memcpy(&transmitBuf[(i * 3) + 2], &sample, 2);
	}
	for (int i = 0; i < adcEndpointCount; i++) {
		if(sendto(adcEndpoints[i].socket, transmitBuf, (3 * adcCount) + 1, 0, 
							(struct sockaddr *)&adcEndpoints[i].sockAddr, sizeof(struct sockaddr)) != (3 * adcCount) + 1) {
			perror("Mismatch in number of ADC bytes sent");
		}
	}
}

timer_t timerid;
int main(int argc, char **argv) {
	int opt;
	char host[32];
	int port = -1;
	char *pairbuf;
	long int seconds = -1;
	long int nanoseconds = -1;
	
	double frequencyHz, timerPeriodSeconds, timerPeriodNanoseconds;
	
	memset(host, 0, sizeof(host));
	memset(adc, 0xFF, sizeof(adc));
  while ((opt = getopt(argc, argv, "vf:h:p:n:")) != -1) {
  	switch (opt) {
			case 'h':
	  			pairbuf=new char[strlen(optarg) + 1];
	  			strcpy(pairbuf, optarg);
	  			adcEndpointCount = parseHostPortPairList(pairbuf, adcEndpoints);
	  			if (adcEndpointCount <= 0) {
						cerr << "-l <adcEndpointList> was missing or improperly formatted, use host1:port1[,host2:port2[,...]]" 
						     << endl << endl;
						Usage();
						exit(-1);
	  			}
	  			delete[] pairbuf;
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
			case 'n':
				adcCount = atoi(optarg);
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
  if (adcEndpointCount == 0 || adcCount <= 0) {
  	Usage();
  	exit(-1);
  }

	if (verbose) {
		cout << "Target Host: " << host << ":" << port << endl;
	}
	//exit(1);
  //MulticastClient *client = new MulticastClient((char *)"226.0.0.1", (short)5800, (char *)"eth0");
	// Create the sockets we'll need to communicate with the specified endpoints
	for (int i = 0; i < adcEndpointCount; i++) initialize_udp_endpoint(&adcEndpoints[i]);

	for (int i = 0; i < adcCount; i++) {
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

