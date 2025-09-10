# TCP Client Test

This project implements a TCP client application that connects to a TCP server, sends a request, and handles the response. The client is designed to test the functionality of the `temp_sens.c` server.

## Project Structure

```
tcp-client-test
├── src
│   ├── main.c          # Main function for the TCP client application
│   └── types
│       └── index.h     # Header file for type definitions and function prototypes
├── Makefile             # Build instructions for the TCP client application
└── README.md            # Project documentation
```

## Requirements

- A C compiler (e.g., GCC)
- Networking capabilities (ensure the server is reachable)

## Building the Project

To build the TCP client application, navigate to the project directory and run:

```
make
```

This will compile the source files and create the executable.

## Running the Client

To run the TCP client, execute the following command:

```
./tcp-client
```

Make sure to replace `tcp-client` with the actual name of the compiled executable if it differs.

## Configuration

Before running the client, ensure that the server's IP address and port are correctly specified in the `src/main.c` file. The default port used by the server is `4242`.

## Dependencies

This project does not have any external dependencies beyond standard C libraries. Ensure that your development environment is set up for C programming.