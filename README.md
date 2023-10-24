# webserver
A hobby implementation of an HTTP webserver written purely in C.

Current limitations:
- Only supports HTTP 1.1
- Only works on windows due to reliance on the winsock api.

Ideas of things to add:

- Arena based memory management for HTTP request/response lifecycle
- Multithreading
- Static file serving
- Request route binding
- HTTPS support
