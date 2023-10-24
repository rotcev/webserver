#include <stdio.h>
#include <winsock2.h>
#include <windows.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#define STATUS_TABLE_SIZE 512

/* Structure to represent an HttpStatus code and it's text form */
typedef struct HttpStatus {
    int code;
    const char *reason;
} HttpStatus;
static HttpStatus statuses[STATUS_TABLE_SIZE];

/* Structure to hold server configuration settings. */
typedef struct Configuration {
    int port;            /* Port number on which the server will listen. */
    int max_reqsize;     /* Maximum allowed size for an incoming request. */
    int max_ressize;     /* Maximum allowed size for an outbound response. */
    bool listening;      /* Flag indicating if the server is listening for connections. */
} Configuration;

/* Structure to represent an HTTP header key-value pair. */
typedef struct Header {
    char *name;          /* Name of the header. */
    char *value;         /* Value of the header. */
    struct Header *next; /* Pointer to the next header in a linked list. */
} Header;

/* Structure to represent an HTTP request. */
typedef struct Request {
    const SOCKET *connection;  /* Socket connection associated with the request. */
    int header_count;    /* Number of headers in the request. */
    Header *headers;     /* Linked list of headers. */
    char *method;        /* HTTP method (e.g., GET, POST). */
    char *path;          /* Requested path (e.g., /index.html). */
    char *http_version;  /* HTTP version (e.g., HTTP/1.1). */
} Request;

typedef struct Response {
    const SOCKET *connection;
    int header_count;
    Header *headers;       /* Linked list of headers */
    int code;              /* HTTP status code */
    char *http_version;    /* String representation of HTTP version (e.g., "HTTP/1.1") */
    char *body;            /* Response body */
    char *encoded_response;
} Response;

/* Removes leading and trailing whitespaces from a string. */
char *trim_whitespace(char *str);

/* Frees resources associated with a Request object. */
void free_request(Request *request);

/* Parses the initial request line (e.g., GET /index.html HTTP/1.1). */
bool parse_status_line(char *line, Request *request);

/* Parses an individual header line and adds it to the Request. */
bool parse_header_line(char *line, Request *request);

/* Reads data from the client socket, splitting it into headers and body. */
void http_decode(SOCKET sock, Configuration *configuration, char **headers_out, char **body_out);

/* Constructs a Request object from the provided headers. */
Request new_request(const SOCKET *connection, char *headers);

/* Handles an incoming request, processing headers and body. */
void handle_request(SOCKET connection, Configuration *configuration);

/* Appends a CRLF sequence to the provided content string */
char *crlf(char *content);

/* Sets an HTTP response header to be eventually returned to the client. */
void add_header(Response *response, const char *name, const char *value);

/* Displays details of the processed request. */
void display_request_details(Request *request, char *body);

/* Sends a response to the client. In this case, a "204 No Content" response. */
void send_response(Configuration *configuration, Response *response);

/* Frees resources once a request has been processed. */
void free_request_resources(Request *request, char *headers, char *body);

/* Frees a response. So far it is assumed that Response structs are allocated on the stack. */
void free_response(Response *response);

/* Sets up the server socket with the specified configuration. */
SOCKET setup_server(Configuration *configuration);

/**
 * @brief Initializes the global `statuses` table with predefined HTTP status codes and their associated reason phrases.
 *
 * @note This function should be called during server initialization to ensure that the status table
 *       is correctly populated before handling any HTTP requests.
 */
void init_status_tbl() {
    memset(statuses, 0, sizeof(statuses));
    statuses[200 % STATUS_TABLE_SIZE] = (HttpStatus) {200, "OK"};
    statuses[201 % STATUS_TABLE_SIZE] = (HttpStatus) {201, "Created"};
    statuses[204 % STATUS_TABLE_SIZE] = (HttpStatus) {204, "No Content"};
}

/**
 * @brief Retrieves the reason phrase for a given HTTP status code.
 *
 * @param code The HTTP status code for which the reason phrase is to be retrieved.
 *
 * @return The reason phrase associated with the provided HTTP status code.
 *         If the status code is not found in the `statuses` array, the function returns NULL.
 */
const char *reason(int code) {
    int index = code % STATUS_TABLE_SIZE;
    if (statuses[index].code == code) {
        return statuses[index].reason;
    }
    return NULL;
}

/**
 * Appends a CRLF (Carriage Return Line Feed) sequence to the given content.
 *
 * @param content The content to which the CRLF sequence will be appended.
 * @return A newly allocated string containing the content followed by a CRLF sequence.
 */
char *crlf(char *content) {
    size_t content_length = strlen(content);
    char *concatenated = malloc(content_length + 3);

    if (concatenated == NULL) {
        return NULL;
    }

    strcpy(concatenated, content);
    concatenated[content_length] = '\r';
    concatenated[content_length + 1] = '\n';
    concatenated[content_length + 2] = '\0';
    return concatenated;
}

/**
 * Adds a new header to the given response structure.
 *
 * @param response The response structure to which the header will be added.
 * @param name The name of the header.
 * @param value The value of the header.
 */
void add_header(Response *response, const char *name, const char *value) {
    Header *new_header = malloc(sizeof(Header));
    new_header->name = strdup(name);
    new_header->value = strdup(value);
    new_header->next = NULL;

    if (!response->headers) {
        response->headers = new_header;
    } else {
        Header *current = response->headers;
        while (current->next) {
            current = current->next;
        }
        current->next = new_header;
    }
    response->header_count++;
}

/**
 * Constructs the status line for a response based on its HTTP status code.
 *
 * @param response The response structure containing the HTTP status code.
 * @return A newly allocated string representing the status line.
 */
char *construct_status_line(Response *response) {
    const char *reason_str = reason(response->code);
    if (!reason_str) {
        printf("Unrecognized reason for code: %d", response->code);
        return NULL;
    }

    char status[64];
    sprintf(status, "HTTP/1.1 %d %s", response->code, reason_str);

    return crlf(status);
}

/**
 * Constructs a string containing all the headers in the given response.
 *
 * @param response The response structure containing the headers.
 * @return A newly allocated string containing all the headers.
 */
char *construct_headers(Response *response) {
    char *headers_str = malloc(response->header_count * 2 * 80);  // Assuming each header won't exceed 160 characters
    headers_str[0] = '\0';  // Start with empty string

    Header *current = response->headers;
    while (current) {
        strcat(headers_str, current->name);
        strcat(headers_str, ": ");
        strcat(headers_str, current->value);
        strcat(headers_str, crlf(""));
        current = current->next;
    }

    return headers_str;
}

/**
 * Encodes the given response into an HTTP-compliant format.
 *
 * @param response The response structure to be encoded.
 */
void encode(Response *response) {
    char *status_line = construct_status_line(response);
    if (!status_line) {
        return;
    }

    size_t body_length = strlen(response->body);
    char body_str[20];
    sprintf(body_str, "%zu", body_length);

    add_header(response, "Content-Length", body_str);
    add_header(response, "Connection", "close");

    char *headers_str = construct_headers(response);

    char *encoded_response = malloc(strlen(status_line) + strlen(headers_str) + body_length + 4);
    strcpy(encoded_response, status_line);
    strcat(encoded_response, headers_str);
    strcat(encoded_response, crlf(""));
    strcat(encoded_response, response->body);

    free(status_line);
    free(headers_str);

    response->encoded_response = encoded_response;
}

/**
 * Frees all resources associated with a given request, including its headers.
 *
 * @param request The request structure to be freed.
 */
void free_request(Request *request) {
    Header *current = request->headers;
    while (current) {
        Header *next = current->next;
        free(current->name);
        free(current->value);
        free(current);
        current = next;
    }
    free(request->http_version);
    free(request->method);
    free(request->path);
    closesocket(*request->connection);
}

/**
 * Decodes the incoming data from a client socket.
 * Separates the received data into headers and body.
 *
 * @param sock Socket to read data from.
 * @param configuration Pointer to the server's configuration.
 * @param headers_out Pointer to store the parsed headers.
 * @param body_out Pointer to store the parsed body.
 */
void http_decode(SOCKET sock, Configuration *configuration, char **headers_out, char **body_out) {
    char buffer[configuration->max_reqsize];
    int read_ptr = 0;

    // Continue reading from the socket until the buffer is almost full.
    while (read_ptr < configuration->max_reqsize - 1) {
        int bytes_read = recv(sock, buffer + read_ptr, configuration->max_reqsize - read_ptr, 0);

        // If no bytes are read or an error occurs, stop reading.
        if (bytes_read <= 0) {
            break;
        }
        read_ptr += bytes_read;

        // Search for the separator between headers and body.
        char *body_start = strstr(buffer, "\r\n\r\n");
        if (body_start) {
            // Calculate the length of the headers.
            ptrdiff_t headers_length = body_start - buffer;

            // Allocate space and copy the headers to the output.
            *headers_out = (char *) malloc(headers_length + 1);
            strncpy(*headers_out, buffer, headers_length);
            (*headers_out)[headers_length] = '\0';

            // Copy the body to the output.
            *body_out = strdup(body_start + 4);
            return;
        }
    }

    // Null-terminate the buffer and assign to the headers output if there was data read.
    buffer[read_ptr] = '\0';
    *headers_out = read_ptr > 0 ? strdup(buffer) : NULL;
    *body_out = NULL;
}

/**
 * Trims leading and trailing whitespace from a string.
 *
 * @param str Input string.
 * @return Pointer to the trimmed string.
 */
char *trim_whitespace(char *str) {
    while (isspace((unsigned char) *str)) {
        str++;
    }
    if (*str == 0) {
        return str;
    }
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char) *end)) {
        end--;
    }
    end[1] = '\0';
    return str;
}

/**
 * Initializes a new request structure from the given connection and headers.
 *
 * @param connection Pointer to the socket connection.
 * @param headers Request headers.
 * @return Initialized Request structure.
 */
Request new_request(const SOCKET *connection, char *headers) {
    Request request = {
            .connection = connection,
            .method = NULL,
            .path = NULL,
            .http_version = NULL,
            .header_count = 0,
            .headers = NULL
    };

    char *saveptr;
    char *line = strtok_r(headers, "\n", &saveptr);

    if (!line || !parse_status_line(line, &request)) {
        return request;
    }

    while ((line = strtok_r(NULL, "\n", &saveptr))) {
        if (!parse_header_line(line, &request)) {
            printf("Malformed request. Failed to parse header line: %s\n", line);
            request.method = NULL;
            break;
        }
    }

    return request;
}

/**
 * Parses the status line of an HTTP request.
 *
 * @param line Input line from the request.
 * @param request Pointer to the Request structure to populate.
 * @return True if parsing is successful, otherwise false.
 */
bool parse_status_line(char *line, Request *request) {
    char *space1 = strchr(line, ' ');
    if (!space1) {
        return false;
    }

    *space1 = '\0';
    request->method = strdup(line);

    char *space2 = strchr(space1 + 1, ' ');
    if (!space2) {
        return false;
    }

    *space2 = '\0';
    request->path = strdup(space1 + 1);
    request->http_version = strdup(space2 + 1);
    return true;
}

/**
 * Parses a single header line and adds it to the given Request structure.
 *
 * @param line Input header line.
 * @param request Pointer to the Request structure to populate.
 * @return True if parsing is successful, otherwise false.
 */
bool parse_header_line(char *line, Request *request) {
    char *colon = strchr(line, ':');
    if (!colon) {
        return false;
    }

    *colon = '\0';
    char *name = trim_whitespace(line);
    char *value = trim_whitespace(colon + 1);

    Header *header = malloc(sizeof(Header));
    header->name = strdup(name);
    header->value = strdup(value);
    header->next = request->headers;
    request->headers = header;
    request->header_count++;

    return true;
}

/**
 * Sets up the server socket based on the provided configuration.
 *
 * @param configuration Pointer to the server's configuration.
 * @return Created socket or INVALID_SOCKET if there's an error.
 */
SOCKET setup_server(Configuration *configuration) {
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(configuration->port);

    SOCKET sock = socket(server_addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        printf("Unable to create socket!");
        return INVALID_SOCKET;
    }

    if (bind(sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Failed to bind to port %d with error: %d\n", configuration->port, WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        printf("Listen failed with error: %d\n", WSAGetLastError());
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

/**
 * Handles the HTTP request from a client connection.
 *
 * @param connection Client's socket connection.
 * @param configuration Pointer to the server's configuration.
 */
void handle_request(SOCKET connection, Configuration *configuration) {
    char *headers = NULL;
    char *body = NULL;

    http_decode(connection, configuration, &headers, &body);

    if (!headers) {
        printf("Failed to parse headers from client");
        closesocket(connection);
        return;
    }

    Request request = new_request(&connection, headers);

    if (request.method != NULL) {
        display_request_details(&request, body);

        Response response = {
                .connection = request.connection,
                .http_version = strdup(request.http_version),
                .body = strdup("<html><body><h4>Hello</h4></body></html>"),
                .code = 200
        };
        send_response(configuration, &response);
    }

    free_request_resources(&request, headers, body);
}

/**
 * Displays the details of the incoming HTTP request for debugging purposes.
 *
 * @param request Pointer to the Request structure.
 * @param body Request body.
 */
void display_request_details(Request *request, char *body) {
    printf("Resource requested: %s %s %s\n", request->method, request->path, request->http_version);
    printf("Headers:\n");
    Header *current = request->headers;
    while (current) {
        printf("\t- %s: %s\n", current->name, current->value);
        current = current->next;
    }

    printf("Body:\n");
    if (body) {
        printf("%s\n\n", body);
    }
}

/**
 * Sends the constructed HTTP response back to the client.
 *
 * @param configuration Pointer to the global configuration.
 * @param response Pointer to the Response structure.
 */
void send_response(Configuration *configuration, Response *response) {
    encode(response);

    if (!response->encoded_response) {
        printf("No encoded response to send");
        free_response(response);
        return;
    }
    size_t response_length = strlen(response->encoded_response);

    if (response_length >= configuration->max_ressize) {
        printf("Attempted to send response that exceeds configuration->max_ressize. Response size: %zu", response_length);
        free_response(response);
        return;
    }

    int bytes_sent = send(*response->connection, response->encoded_response, (int) response_length, 0);
    if (bytes_sent == SOCKET_ERROR) {
        printf("Unable to write response to client, failed with error: %d\n", WSAGetLastError());
        free_response(response);
        return;
    }

    if (bytes_sent != (int) response_length) {
        printf("Warning: Not all bytes sent. Bytes sent: %d, Expected: %zu\n", bytes_sent, response_length);
    }
    free_response(response);
}

/**
 * Frees the resources associated with the given request, headers, and body.
 *
 * @param request Pointer to the Request structure to be freed.
 * @param headers Request headers to be freed.
 * @param body Request body to be freed.
 */
void free_request_resources(Request *request, char *headers, char *body) {
    free_request(request);
    if (headers) free(headers);
    if (body) free(body);
}

/**
 * Frees the resources associated with the given response.
 *
 * @param response Pointer to the Response structure to be freed.
 */
void free_response(Response *response) {
    if (!response) {
        return;
    }

    Header *current = response->headers;
    while (current) {
        Header *next_header = current->next;
        free(current->name);
        free(current->value);
        free(current);
        current = next_header;
    }

    free(response->http_version);
    free(response->body);
    free(response->encoded_response);
}

/**
 * Entry point for the server application.
 * Initializes the server, listens for incoming connections, and handles requests.
 *
 * @return 0 on successful execution, 1 otherwise.
 */
int main() {
    init_status_tbl();
    WSADATA wsa;
    /* Initialize Winsock. */
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return 1;
    }

    Configuration configuration = {
            .port = 80,
            .max_reqsize = 8192,
            .max_ressize = 8192,
            .listening = true
    };

    /* Setup the server with the specified configuration. */
    SOCKET sock = setup_server(&configuration);
    if (sock == INVALID_SOCKET) {
        return 1;
    }

    /* Listen for and accept incoming connections, then handle each request. */
    while (configuration.listening) {
        struct sockaddr_in client_addr;
        int client_addr_size = sizeof(client_addr);
        SOCKET connection = accept(sock, (struct sockaddr *) &client_addr, &client_addr_size);

        if (connection != INVALID_SOCKET) {
            handle_request(connection, &configuration);
        }
    }

    return 0;
}
